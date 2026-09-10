#!/usr/bin/env python3
"""Off is still today's shader.

    tools/shader_offpath_check.py            # check, exit non-zero on a change
    tools/shader_offpath_check.py --update   # re-archive, after a deliberate change

WHY

The rule the whole modern-rendering plan stands on (docs/plan-modern-rendering.md
§6.3): a technique that is switched off leaves the frame exactly as it was.
Every toggle added from phase 01 on is a specialization constant whose default
is what the client did before, so an unspecialized pipeline is the shader that
shipped - and that promise is worth nothing unless something measures it.

Nothing did. A branch moved inside an `if (FEATURE)` that was never quite the
same shape as the code it replaced, a constant folded slightly differently, a
function extracted into an include with one operand reordered: none of those
fail to compile, none of them raise, and the difference is a few pixels on a
screenshot nobody is comparing.

WHAT IT COMPARES

For each shader under tests/shaders_prephase/ - the archived source as it was
before the phase that touched it:

    reference = glslc -O <archived source>            then spirv-opt -O
    current   = glslc -O -I assets/shaders <current>  then spirv-opt
                --freeze-spec-const --fold-spec-const-op-composite -O

Freezing is what makes this a fair comparison. A specialization constant is
still a runtime value inside the module; the driver folds it when the pipeline
is created. spirv-opt --freeze-spec-const does the same thing offline with the
declared defaults, which is exactly the variant a caller passing no
VkSpecializationInfo gets.

The two are then compared as the multiset of their function-body instructions,
with every result id erased. That is insensitive to id renumbering and to basic
blocks being emitted in a different order - both of which spirv-opt does
routinely and neither of which changes what the shader computes - and sensitive
to an instruction added, removed or given a different literal.

WHAT IT DOES NOT COVER

Only the code. A uniform block that grew by a member no branch reads is not a
difference here, because it is not one: std140 offsets of the members that were
already there do not move when a block is appended to, which is the append-only
rule in §6.4. Decorations, entry-point interfaces and debug names are outside
the function bodies and are not compared.

And it says nothing about a change that is *meant* to happen. When a phase
deliberately changes what a shader does with everything off - which should be
close to never - re-archive with --update, in the same commit, with the reason
in the commit message.

REQUIREMENTS

glslc, spirv-opt and spirv-dis, from the Vulkan SDK or the shaderc and
spirv-tools packages. Without them the check skips and says so rather than
passing quietly: a check that cannot run is not a check that passed.
"""
import argparse
import collections
import os
import pathlib
import re
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SHADER_DIR = ROOT / "assets" / "shaders"
ARCHIVE_DIR = ROOT / "tests" / "shaders_prephase"


def find_tool(name):
    """glslc and friends, from PATH or from the Vulkan SDK the build used."""
    found = shutil.which(name)
    if found:
        return found
    sdk = os.environ.get("VULKAN_SDK")
    if sdk:
        for sub in ("Bin", "bin"):
            for ext in ("", ".exe"):
                candidate = pathlib.Path(sdk) / sub / (name + ext)
                if candidate.is_file():
                    return str(candidate)
    return None


def stage_of(name):
    for suffix, stage in (("vert", "vertex"), ("frag", "fragment"),
                          ("comp", "compute"), ("geom", "geometry")):
        if ".%s." % suffix in name:
            return stage
    return None


def compile_spv(glslc, source, out, include_dir=None):
    cmd = [glslc, "-fshader-stage=%s" % stage_of(source.name)]
    if include_dir is not None:
        cmd += ["-I", str(include_dir)]
    cmd += ["-O", str(source), "-o", str(out)]
    subprocess.run(cmd, check=True, capture_output=True, text=True)


def optimise(spirv_opt, spv, out, freeze):
    cmd = [spirv_opt]
    if freeze:
        # Two passes, not one. --freeze-spec-const turns the constants into
        # ordinary ones but leaves an OpSpecConstantOp behind wherever the
        # shader compared one - `SPEC_FOG_MODEL == 1` becomes
        # `OpSpecConstantOp IEqual %int_0 %int_1`, which -O does not fold and
        # which therefore kept a whole dead branch alive in the comparison.
        cmd += ["--freeze-spec-const", "--fold-spec-const-op-composite"]
    cmd += ["-O", str(spv), "-o", str(out)]
    subprocess.run(cmd, check=True, capture_output=True, text=True)


def body_multiset(spirv_dis, spv):
    """The function bodies, as a sorted list of id-erased instructions."""
    text = subprocess.run([spirv_dis, str(spv), "-o", "-"],
                          check=True, capture_output=True, text=True).stdout
    start = text.find("OpFunction")
    body = text[start:] if start >= 0 else text
    return sorted(re.sub(r"%\w+", "%", line.strip())
                  for line in body.splitlines() if line.strip())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--update", action="store_true",
                        help="re-archive the current sources as the new reference")
    args = parser.parse_args()

    if args.update:
        for archived in sorted(ARCHIVE_DIR.glob("*.glsl")):
            live = SHADER_DIR / archived.name
            if not live.is_file():
                print("%s has no shader beside it any more" % archived.name)
                continue
            archived.write_text(live.read_text())
            print("re-archived %s" % archived.name)
        return 0

    glslc = find_tool("glslc")
    spirv_opt = find_tool("spirv-opt")
    spirv_dis = find_tool("spirv-dis")
    missing = [n for n, t in (("glslc", glslc), ("spirv-opt", spirv_opt),
                              ("spirv-dis", spirv_dis)) if t is None]
    if missing:
        print("SKIPPED: %s not found (Vulkan SDK or shaderc/spirv-tools)"
              % ", ".join(missing))
        # Not a pass. The caller that cares - CI - has the SDK; a developer
        # without it should see the word and not a green tick.
        return 0

    work = pathlib.Path(os.environ.get("TMPDIR") or os.environ.get("TEMP") or "/tmp")
    work = work / "wowee_offpath"
    work.mkdir(parents=True, exist_ok=True)

    archived = sorted(ARCHIVE_DIR.glob("*.glsl"))
    if not archived:
        print("no archived shaders in %s" % ARCHIVE_DIR)
        return 1

    failures = 0
    for reference in archived:
        name = reference.name
        live = SHADER_DIR / name
        if not live.is_file():
            print("FAIL %s: archived, but no shader of that name any more" % name)
            failures += 1
            continue
        try:
            compile_spv(glslc, reference, work / (name + ".ref.spv"))
            compile_spv(glslc, live, work / (name + ".cur.spv"), SHADER_DIR)
            optimise(spirv_opt, work / (name + ".ref.spv"),
                     work / (name + ".ref.opt.spv"), freeze=False)
            optimise(spirv_opt, work / (name + ".cur.spv"),
                     work / (name + ".cur.opt.spv"), freeze=True)
        except subprocess.CalledProcessError as exc:
            print("FAIL %s: %s" % (name, (exc.stderr or "").strip() or exc))
            failures += 1
            continue

        ref = body_multiset(spirv_dis, work / (name + ".ref.opt.spv"))
        cur = body_multiset(spirv_dis, work / (name + ".cur.opt.spv"))
        if ref == cur:
            print("ok   %s (%d instructions)" % (name, len(ref)))
            continue

        failures += 1
        only_ref = collections.Counter(ref) - collections.Counter(cur)
        only_cur = collections.Counter(cur) - collections.Counter(ref)
        print("FAIL %s: the off-path is not the shader that shipped" % name)
        print("     %d instructions before, %d now" % (len(ref), len(cur)))
        for line, count in list(only_ref.items())[:8]:
            print("     - x%d %s" % (count, line))
        for line, count in list(only_cur.items())[:8]:
            print("     + x%d %s" % (count, line))

    print("%d shader(s) whose off-path moved" % failures)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

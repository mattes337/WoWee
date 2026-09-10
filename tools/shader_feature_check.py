#!/usr/bin/env python3
"""The two halves of the shader feature set, against each other.

    tools/shader_feature_check.py

WHY

A specialization constant is a number on both sides and a name on neither.
`assets/shaders/shader_features.glsl` declares `layout(constant_id = 17) const
int SPEC_SHADOW_FILTER = 0;` and `include/rendering/shader_features.hpp` says
`ShadowFilter = 17`, and nothing connects them: Vulkan takes a constant id and
four bytes, and a `VkSpecializationMapEntry` naming an id the module does not
declare is not an error - it is ignored, silently, and the shader runs with the
default the GLSL gave it.

So an id typed wrong on one side is a setting that appears to work, saves, and
changes nothing; and two features sharing an id is one of them driving the
other. Neither fails to compile, neither raises, and both look exactly like a
feature that is simply subtle.

The defaults matter as much as the ids. The whole off-path promise
(docs/plan-modern-rendering.md §6.3) is that a pipeline built with no
`VkSpecializationInfo` is the shader that shipped - which is only true while
every GLSL default is the old behaviour, and `ShaderFeatures{}` only reproduces
that while the C++ agrees about what the old behaviour was.

WHAT IT LOOKS FOR

Every `layout(constant_id = N)` in the GLSL, every entry of `ShaderFeatureBit`
and `ShaderFeatureConstant` in the header, and whether the two sets of ids, the
two sets of names and the two sets of defaults are the same.

The names are matched by the one rule the header states: a C++ `ShadowFilter`
is a GLSL `SPEC_SHADOW_FILTER`. A feature named differently on the two sides is
reported rather than guessed at.

WHAT IT CANNOT SEE

Whether a shader that reads a constant should have been reading a different
one, and whether a renderer passes the specialization at all - only that the
two declarations agree.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
GLSL = ROOT / "assets" / "shaders" / "shader_features.glsl"
HPP = ROOT / "include" / "rendering" / "shader_features.hpp"

GLSL_CONSTANT = re.compile(
    r"layout\(constant_id\s*=\s*(\d+)\)\s*const\s+(bool|int)\s+(\w+)\s*=\s*([^;]+);")
CPP_ENUMERATOR = re.compile(r"^\s*(\w+)\s*=\s*(\d+)\s*,", re.MULTILINE)


def spec_name(cpp_name):
    """ShadowFilter -> SPEC_SHADOW_FILTER, the rule the header states."""
    words = re.findall(r"[A-Z]+(?![a-z])|[A-Z][a-z0-9]*|[a-z0-9]+", cpp_name)
    return "SPEC_" + "_".join(w.upper() for w in words)


def cpp_block(text, opener):
    start = text.index(opener)
    return text[start:text.index("};", start)]


def main():
    for path in (GLSL, HPP):
        if not path.is_file():
            print("missing %s" % path.relative_to(ROOT).as_posix())
            return 1

    glsl_text = GLSL.read_text(encoding="utf-8")
    hpp_text = HPP.read_text(encoding="utf-8")

    # ---- the GLSL side ----
    glsl = {}
    for cid, ctype, name, default in GLSL_CONSTANT.findall(glsl_text):
        glsl[name] = (int(cid), ctype, default.strip())

    # ---- the C++ side ----
    bits_block = cpp_block(hpp_text, "enum class ShaderFeatureBit")
    ints_block = cpp_block(hpp_text, "enum class ShaderFeatureConstant")
    cpp = {}
    for name, value in CPP_ENUMERATOR.findall(bits_block):
        if name == "Count":
            continue
        cpp[spec_name(name)] = (int(value), "bool", name)
    for name, value in CPP_ENUMERATOR.findall(ints_block):
        cpp[spec_name(name)] = (int(value), "int", name)

    # ---- the C++ defaults ----
    # The bits that ShaderFeatures{} has on, and the initialiser of each small
    # integer member. Read from the source rather than by building the struct,
    # so this runs without a compiler.
    default_bits = set(re.findall(r"ShaderFeatureBit::(\w+)\)\)",
                                  hpp_text[hpp_text.index("static constexpr uint32_t defaultBits"):
                                           hpp_text.index("constexpr bool has(")]))
    int_defaults = dict(re.findall(r"^\s*int32_t\s+(\w+)\s*=\s*(-?\d+);", hpp_text, re.MULTILINE))

    problems = []

    for name, (cid, ctype, default) in sorted(glsl.items()):
        if name not in cpp:
            problems.append("%s is declared in the GLSL with id %d and has no "
                            "entry in shader_features.hpp" % (name, cid))
            continue
        cpp_id, cpp_type, cpp_name = cpp[name]
        if cpp_id != cid:
            problems.append("%s is constant_id %d in the GLSL and %d in the header"
                            % (name, cid, cpp_id))
        if cpp_type != ctype:
            problems.append("%s is a %s in the GLSL and a %s in the header"
                            % (name, ctype, cpp_type))
        if ctype == "bool":
            glsl_on = default == "true"
            cpp_on = cpp_name in default_bits
            if glsl_on != cpp_on:
                problems.append(
                    "%s defaults to %s in the GLSL and is %s in ShaderFeatures{} - "
                    "the off-path promise is that these agree"
                    % (name, default, "on" if cpp_on else "off"))
        else:
            member = cpp_name[0].lower() + cpp_name[1:]
            if member not in int_defaults:
                problems.append("%s has no int32_t %s with an initialiser in the header"
                                % (name, member))
            elif int_defaults[member] != default:
                problems.append("%s defaults to %s in the GLSL and %s in the header"
                                % (name, default, int_defaults[member]))

    for name, (cid, _, _) in sorted(cpp.items()):
        if name not in glsl:
            problems.append("%s has id %d in the header and is not declared in "
                            "shader_features.glsl" % (name, cid))

    ids = {}
    for name, (cid, _, _) in sorted(glsl.items()):
        ids.setdefault(cid, []).append(name)
    for cid, names in sorted(ids.items()):
        if len(names) > 1:
            problems.append("constant_id %d is used by %s" % (cid, " and ".join(names)))

    print("%d specialization constant(s) declared" % len(glsl))
    print("%d that the two halves disagree about" % len(problems))
    for line in problems:
        print("  " + line)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())

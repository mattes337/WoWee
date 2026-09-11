#!/usr/bin/env python3
"""Two renders of one camera, and what moved between them.

    tools/compare_scenes.py --data Data --map Azeroth \\
        --camera=-9462,-67,70 --target=-9200,-320,50 --time 9 \\
        --setting shadowcascades --off 0 --on 2 \\
        --out docs/evidence/phase-01/img/goldshire-lake-cascades

A coordinate that starts with a minus sign has to be given with an `=`, as
above: argparse reads a bare `-9462,-67,70` as an option name rather than as
the value of the option before it. capture_scene's own parser does not care.

    tools/compare_scenes.py --before a.png --after b.png \\
        --out docs/evidence/phase-01/whatever

WHY

A rendering change is reviewed by looking at it. The diff of the shader says
what the code now does, not whether the picture got better, and a description
of the picture is not evidence of anything. Two PNGs of the same camera with
one setting moved, and a heat map of where they differ, is.

WHAT IT WRITES

    <out>.before.png   the setting at its off value
    <out>.after.png    the setting at its on value
    <out>.diff.png     a heat map: black where nothing moved, through blue and
                       yellow to white where it moved most

and prints the fraction of pixels that changed at all, the mean absolute
difference, and SSIM.

SSIM is the structural similarity index: 1.0 is the same image, and it falls
off with differences a person would notice rather than with differences a
subtractor would. It is computed here on the luminance at an 8x8 window, which
is the usual simplification and is enough to tell "the shadows moved" from "the
whole frame got darker".

REQUIREMENTS

Pillow. numpy if it is there - without it the SSIM is computed in pure Python
over a downscaled image, which is slower and coarser, and the tool says so
rather than skipping the number.

capture_scene, if the renders are to be made rather than handed in. It is off
by default in CMake:

    cmake -S . -B build -DWOWEE_BUILD_CAPTURE_SCENE=ON
    cmake --build build --target capture_scene
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    sys.stderr.write("compare_scenes needs Pillow: pip install pillow\n")
    sys.exit(2)

try:
    import numpy as np
except ImportError:
    np = None


def find_capture_scene(explicit: str | None) -> str | None:
    if explicit:
        return explicit if Path(explicit).exists() else None
    root = Path(__file__).resolve().parent.parent
    for candidate in (
        root / "build" / "bin" / "capture_scene.exe",
        root / "build" / "bin" / "capture_scene",
        root / "build" / "bin" / "Release" / "capture_scene.exe",
    ):
        if candidate.exists():
            return str(candidate)
    return None


def render(exe: str, args: argparse.Namespace, value: str, out_prefix: str) -> None:
    cmd = [exe, "-d", args.data, "-o", out_prefix, "--time", str(args.time)]
    if args.map:
        cmd += ["-m", args.map]
    if args.map_id is not None:
        cmd += ["--map-id", str(args.map_id)]
    cmd += ["-c", args.camera]
    if args.target:
        cmd += ["-t", args.target]
    if args.width:
        cmd += ["--width", str(args.width)]
    if args.height:
        cmd += ["--height", str(args.height)]
    if args.wireframe:
        cmd += ["--wireframe"]
    for extra in args.also or []:
        cmd += ["--setting", extra]
    if args.setting:
        cmd += ["--setting", f"{args.setting}={value}"]
    print("  " + " ".join(cmd))
    subprocess.run(cmd, check=True)


def load_gray(path: Path):
    """Luminance, as a list of rows of floats 0..255, or a numpy array."""
    img = Image.open(path).convert("L")
    if np is not None:
        return np.asarray(img, dtype=float), img.size
    return list(img.getdata()), img.size


def ssim_numpy(a, b) -> float:
    """Global SSIM over 8x8 blocks. C1/C2 are the usual constants for 8-bit."""
    c1 = (0.01 * 255) ** 2
    c2 = (0.03 * 255) ** 2
    h, w = a.shape
    bh, bw = h // 8 * 8, w // 8 * 8
    a = a[:bh, :bw].reshape(bh // 8, 8, bw // 8, 8).transpose(0, 2, 1, 3).reshape(-1, 64)
    b = b[:bh, :bw].reshape(bh // 8, 8, bw // 8, 8).transpose(0, 2, 1, 3).reshape(-1, 64)
    mu_a = a.mean(axis=1)
    mu_b = b.mean(axis=1)
    va = a.var(axis=1)
    vb = b.var(axis=1)
    cov = ((a - mu_a[:, None]) * (b - mu_b[:, None])).mean(axis=1)
    num = (2 * mu_a * mu_b + c1) * (2 * cov + c2)
    den = (mu_a**2 + mu_b**2 + c1) * (va + vb + c2)
    return float((num / den).mean())


def ssim_pure(a, b, size) -> float:
    """The same thing without numpy, over a downscaled pair.

    Slower and coarser: a full 1080p pair in pure Python is minutes, so the
    images are reduced to 320 wide first. Reported as such by the caller.
    """
    c1 = (0.01 * 255) ** 2
    c2 = (0.03 * 255) ** 2
    w, h = size
    stride = max(1, w // 320)
    rows_a, rows_b = [], []
    for y in range(0, h, stride):
        rows_a.append(a[y * w : (y + 1) * w : stride])
        rows_b.append(b[y * w : (y + 1) * w : stride])
    total, blocks = 0.0, 0
    for by in range(0, len(rows_a) - 7, 8):
        for bx in range(0, len(rows_a[0]) - 7, 8):
            pa = [rows_a[by + dy][bx + dx] for dy in range(8) for dx in range(8)]
            pb = [rows_b[by + dy][bx + dx] for dy in range(8) for dx in range(8)]
            ma = sum(pa) / 64.0
            mb = sum(pb) / 64.0
            va = sum((p - ma) ** 2 for p in pa) / 64.0
            vb = sum((p - mb) ** 2 for p in pb) / 64.0
            cov = sum((pa[i] - ma) * (pb[i] - mb) for i in range(64)) / 64.0
            num = (2 * ma * mb + c1) * (2 * cov + c2)
            den = (ma * ma + mb * mb + c1) * (va + vb + c2)
            total += num / den
            blocks += 1
    return total / blocks if blocks else 1.0


def heat(value: float) -> tuple[int, int, int]:
    """Black to blue to yellow to white, so a small difference is still visible
    against a dark frame and a large one is unmistakable."""
    t = max(0.0, min(1.0, value))
    if t < 0.33:
        u = t / 0.33
        return (0, 0, int(255 * u))
    if t < 0.66:
        u = (t - 0.33) / 0.33
        return (int(255 * u), int(255 * u), int(255 * (1 - u)))
    u = (t - 0.66) / 0.34
    return (255, 255, int(255 * u))


def compare(before: Path, after: Path, out: Path) -> int:
    # Built by hand rather than with with_suffix: an output prefix may well
    # carry a dot of its own, and with_suffix would eat everything after it.
    diff_path = Path(str(out) + ".diff.png")
    ia = Image.open(before).convert("RGB")
    ib = Image.open(after).convert("RGB")
    if ia.size != ib.size:
        sys.stderr.write(f"the two renders are different sizes: {ia.size} vs {ib.size}\n")
        return 1

    if np is not None:
        a = np.asarray(ia, dtype=float)
        b = np.asarray(ib, dtype=float)
        delta = np.abs(a - b).max(axis=2)
        changed = float((delta > 1.0).mean())
        mean_abs = float(np.abs(a - b).mean())
        # The heat map is scaled to the largest difference in the frame, so a
        # subtle change is still a picture rather than a black rectangle.
        peak = max(float(delta.max()), 1.0)
        lut = np.array([heat(i / 255.0) for i in range(256)], dtype=np.uint8)
        scaled = np.clip(delta / peak * 255.0, 0, 255).astype(np.uint8)
        Image.fromarray(lut[scaled]).save(diff_path)
        ga = np.asarray(ia.convert("L"), dtype=float)
        gb = np.asarray(ib.convert("L"), dtype=float)
        score = ssim_numpy(ga, gb)
        exact = True
    else:
        pa = list(ia.getdata())
        pb = list(ib.getdata())
        deltas = [max(abs(x - y) for x, y in zip(p, q)) for p, q in zip(pa, pb)]
        changed = sum(1 for d in deltas if d > 1) / float(len(deltas))
        mean_abs = sum(deltas) / float(len(deltas))
        peak = max(max(deltas), 1)
        diff = Image.new("RGB", ia.size)
        diff.putdata([heat(d / peak) for d in deltas])
        diff.save(diff_path)
        ga, size = load_gray(before)
        gb, _ = load_gray(after)
        score = ssim_pure(ga, gb, size)
        exact = False

    print(f"pixels changed : {changed * 100:.3f}%")
    print(f"mean abs diff  : {mean_abs:.3f} / 255")
    print(f"SSIM           : {score:.6f}" + ("" if exact else "   (numpy is not installed:"
          " computed on a 320-wide reduction, so this is coarser than the number"
          " a machine with numpy would print)"))
    print(f"diff written   : {diff_path}")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True, help="output prefix")
    ap.add_argument("--before", help="an already-rendered PNG; skips capture_scene")
    ap.add_argument("--after", help="the other one")
    ap.add_argument("--capture-scene", help="path to the capture_scene executable")
    ap.add_argument("--data", default="Data")
    ap.add_argument("--map", default="Azeroth")
    ap.add_argument("--map-id", type=int, default=None)
    ap.add_argument("--camera", help="x,y,z in server coordinates")
    ap.add_argument("--target", help="x,y,z the camera looks at")
    ap.add_argument("--time", default="12", help="hour of the day, 0-24")
    ap.add_argument("--width", type=int, default=None)
    ap.add_argument("--height", type=int, default=None)
    ap.add_argument("--wireframe", action="store_true")
    ap.add_argument("--setting", help="the key to move between the two renders")
    ap.add_argument("--off", default="0", help="its value for the before render")
    ap.add_argument("--on", default="1", help="its value for the after render")
    ap.add_argument("--also", action="append",
                    help="key=value applied to both renders; repeatable")
    args = ap.parse_args()

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)

    if args.before and args.after:
        return compare(Path(args.before), Path(args.after), out)

    if not args.camera:
        ap.error("either --before/--after, or --camera to render a pair")
    exe = find_capture_scene(args.capture_scene)
    if not exe:
        sys.stderr.write(
            "capture_scene was not found. Build it, or hand in two PNGs:\n"
            "  cmake -S . -B build -DWOWEE_BUILD_CAPTURE_SCENE=ON\n"
            "  cmake --build build --target capture_scene\n")
        return 2

    print(f"before ({args.setting}={args.off}):")
    render(exe, args, args.off, str(out) + ".before")
    print(f"after ({args.setting}={args.on}):")
    render(exe, args, args.on, str(out) + ".after")
    return compare(Path(str(out) + ".before.png"), Path(str(out) + ".after.png"), out)


if __name__ == "__main__":
    raise SystemExit(main())

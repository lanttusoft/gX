#!/usr/bin/env python3
#ai: dev-only module-owned scene smoke — first/third camera + level geometry.

# Runs the real gX binary with the game-side gx_level lab module in both
# camera modes, checks the generic MODULE scene handshake, verifies projected
# level geometry, and proves the two captures differ. No shipped payload or
# asset path is touched.

import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

try:
    from PIL import Image, ImageChops
except ImportError as exc:
    raise SystemExit(f"level_module_check: Pillow is required: {exc}")

ROOT = Path(__file__).resolve().parent.parent


def fail(message):
    print(f"LEVEL_MODULE_CHECK: FAIL — {message}", file=sys.stderr)
    return 1


def geometry_pixels(image):
    width, height = image.size
    pixels = image.load()
    count = 0
    for y in range(120, 680):
        for x in range(24, min(width - 260, 760)):
            r, g, b = pixels[x, y]
            if g > 110 and g > r + 30 and g > b + 15:
                count += 1
    return count


def run_case(engine, module, level, display, camera):
    with tempfile.TemporaryDirectory(prefix=f"gx-level-{camera}-") as tmp:
        env = dict(os.environ)
        env.update({
            "DISPLAY": display,
            "GX_AUDIT_WINDOW": "hidden",
            "GX_FRAMES": "30",
            "GX_LEVEL": str(level.resolve()),
            "GX_LEVEL_CAMERA": camera,
            "GX_NO_SETTINGS_SAVE": "1",
            "GX_SCREENSHOT_DIR": tmp,
        })
        command = [
            str(engine.resolve()),
            "--module", str(module.resolve()),
            "--module-scene",
            "--ai-screenshot-after", "20",
            "--screenWidth", "1024",
            "--screenHeight", "768",
        ]
        result = subprocess.run(
            command, cwd=ROOT, env=env, capture_output=True, text=True,
            timeout=60)
        output = result.stdout + result.stderr
        if result.returncode != 0:
            raise RuntimeError(
                f"{camera}: engine exited {result.returncode}")
        required = (
            "LEVEL_MODULE: loaded",
            f"camera {camera}",
            "LEVEL_MODULE: draw 1024x768",
            "switched to masterIndex",
            "[MODULE]",
            "MAIN: Masterindex switch successful",
        )
        for needle in required:
            if needle not in output:
                raise RuntimeError(f"{camera}: missing log marker: {needle}")
        shots = sorted(Path(tmp).glob("*.png"))
        if not shots:
            raise RuntimeError(f"{camera}: no screenshot was written")
        image = Image.open(shots[-1]).convert("RGB")
        if image.size != (1024, 768):
            raise RuntimeError(f"{camera}: unexpected capture {image.size}")
        geometry = geometry_pixels(image)
        if geometry < 100:
            raise RuntimeError(
                f"{camera}: level geometry band is empty ({geometry})")
        return image, geometry, shots[-1].name


def main():
    parser = argparse.ArgumentParser(description="gX level-module smoke")
    parser.add_argument("engine", type=Path)
    parser.add_argument("module", type=Path)
    parser.add_argument("level", type=Path)
    parser.add_argument("--display", default=os.environ.get("DISPLAY", ":1"))
    args = parser.parse_args()

    for path in (args.engine, args.module, args.level):
        if not path.is_file():
            return fail(f"missing {path}")

    try:
        first, first_geometry, first_name = run_case(
            args.engine, args.module, args.level, args.display, "first")
        third, third_geometry, third_name = run_case(
            args.engine, args.module, args.level, args.display, "third")
    except (OSError, RuntimeError, subprocess.SubprocessError) as exc:
        return fail(str(exc))

    difference = ImageChops.difference(first, third)
    histogram = difference.convert("L").histogram()
    changed = sum(histogram[5:])
    fraction = changed / float(first.size[0] * first.size[1])
    if fraction < 0.01:
        return fail(f"first/third captures are too similar ({fraction:.4f})")

    print(
        f"LEVEL_MODULE_CHECK: PASS — first {first_geometry} px "
        f"({first_name}), third {third_geometry} px ({third_name}), "
        f"changed {fraction:.3f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

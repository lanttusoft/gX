#!/usr/bin/env python3
#opencode: decal-bake gate. The editor hook places two known decals and bakes
#   them into a temp folder; the gate asserts the PNGs, the coverage and the
#   albedo/ORM channel values.

import re
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image

import audit_common as audit


ROOT = audit.ROOT
BIN = audit.BIN
HOOK_RE = re.compile(r"GFX_DECAL_BAKE_CHECK: (PASS|FAIL) albedo=(.+)")


def main():
    if not BIN.is_file():
        print("GFX_DECAL_BAKE_CHECK: missing binary", file=sys.stderr)
        return 1
    try:
        with tempfile.TemporaryDirectory(prefix="gfx_decal_bake_",
                                         dir="/tmp/opencode") as temporary:
            folder = Path(temporary)
            environment = audit.scene_env({
                "GX_FIXED_DT": "0.0166666667",
                "GX_FRAMES": "20",
                "GX_GFX_DECAL_BAKE_CHECK": str(folder),
            }, clean=True)
            with audit.settings_preserved():
                result = audit.run_scene("--gfx", environment, width=None,
                    height=None, fps=None, frames=20, timeout=120, check=True,
                    extra_args=("--gfx-editor",))
            log = audit.strip_ansi(result.stdout or "")
            hook = HOOK_RE.findall(log)
            if not hook or hook[-1][0] != "PASS":
                raise RuntimeError(f"bake hook did not pass: {hook}")
            albedo_path = folder / "bake_albedo.png"
            orm_path = folder / "bake_orm.png"
            if not albedo_path.is_file() or not orm_path.is_file():
                raise RuntimeError("bake files missing")
            albedo = np.asarray(Image.open(albedo_path).convert("RGBA"),
                                dtype=np.int16)
            orm = np.asarray(Image.open(orm_path).convert("RGBA"),
                             dtype=np.int16)
            if albedo.shape != (128, 128, 4) or orm.shape != (128, 128, 4):
                raise RuntimeError(f"unexpected shapes {albedo.shape} "
                                   f"{orm.shape}")
            if tuple(albedo[64, 64]) != (200, 60, 40, 255):
                raise RuntimeError(f"albedo center {albedo[64, 64]}")
            if int(albedo[0, 0][3]) != 0:
                raise RuntimeError("corner is covered")
            covered = float((albedo[..., 3] > 0).mean())
            if not 0.05 <= covered <= 0.9:
                raise RuntimeError(f"coverage fraction {covered:.3f}")
            if tuple(orm[64, 64]) != (255, 191, 64, 255):
                raise RuntimeError(f"orm center {orm[64, 64]}")
            if tuple(orm[0, 0]) != (255, 255, 0, 255):
                raise RuntimeError(f"orm corner {orm[0, 0]}")
    except (OSError, RuntimeError, ValueError) as error:
        print(f"GFX_DECAL_BAKE_CHECK: FAIL — {error}", file=sys.stderr)
        return 1
    print(f"GFX_DECAL_BAKE_CHECK: PASS albedo + ORM baked "
          f"(coverage {covered:.3f}, channels exact)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

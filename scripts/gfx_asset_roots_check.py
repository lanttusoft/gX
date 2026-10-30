#!/usr/bin/env python3
#opencode: browse-only asset root discovery pin. The GFX asset browser indexes
#   local resources plus guest roots from three sources: GX_ASSETS, the
#   --guest-asset flags, and the settings-backed root list the browser's
#   "Add root" button writes (GX_GFX_ASSET_ROOT_ADD here). Roots are read in
#   place — nothing is copied into the project — so this gate builds throwaway
#   trees under /tmp and asserts the scan finds their model/texture files.
#
#   Usage: DISPLAY=:1 python3 scripts/gfx_asset_roots_check.py
#   GPU/display required — use `make gfx-asset-roots-check`.

import re
import shutil
import sys
import tempfile
from pathlib import Path

import audit_common as audit


ROOT = audit.ROOT
PROBE = ROOT / "resources" / "model" / "probe" / "animprobe.glb"
TEXTURE = ROOT / "resources" / "texture" / "orb.png"
SCAN_RE = re.compile(
    r"GfxEd: assets scan roots=(\d+) models=(\d+) textures=(\d+) "
    r"guestModels=(\d+) guestTextures=(\d+) generated=(\d+)"
)


def main():
    if not audit.BIN.is_file() or not PROBE.is_file() or not TEXTURE.is_file():
        print("GFX_ASSET_ROOTS_CHECK: missing binary or fixture", file=sys.stderr)
        return 1
    try:
        with tempfile.TemporaryDirectory(prefix="gfx_asset_roots_",
                                         dir="/tmp/opencode") as temporary:
            temp = Path(temporary)
            env_root = temp / "env_root"
            settings_root = temp / "settings_root"
            (env_root / "model").mkdir(parents=True)
            (env_root / "texture").mkdir(parents=True)
            (settings_root / "model").mkdir(parents=True)
            shutil.copyfile(PROBE, env_root / "model" / "envprobe.glb")
            shutil.copyfile(TEXTURE, env_root / "texture" / "envorb.png")
            shutil.copyfile(PROBE, settings_root / "model" / "settingsprobe.glb")

            #opencode: populate the generated-textures folder first — the
            #   browser's local texture grid must index it at startup.
            generated_dir = temp / "generated"
            generated_dir.mkdir()
            with audit.settings_preserved():
                audit.run_scene("--gfx", audit.scene_env({
                    "GX_FIXED_DT": "0.0166666667",
                    "GX_FRAMES": "12",
                    "GX_GFX_TEXTURE_CHECK": "1",
                    "GX_GFX_TEXTURE_DIR": str(generated_dir),
                }, clean=True), width=None, height=None, fps=None,
                    timeout=120, check=True, extra_args=("--gfx-editor",))
            with audit.settings_preserved():
                environment = audit.scene_env({
                    "GX_FIXED_DT": "0.0166666667",
                    "GX_FRAMES": "60",
                    "GX_ASSETS": str(env_root),
                    "GX_GFX_ASSET_ROOT_ADD": str(settings_root),
                    "GX_ASSET_TRACE": "1",
                    "GX_GFX_TEXTURE_DIR": str(generated_dir),
                }, clean=True)
                result = audit.run_scene("--gfx", environment, width=None,
                                         height=None, fps=None, timeout=120,
                                         check=True,
                                         extra_args=("--gfx-editor",))
            log = audit.strip_ansi(result.stdout or "")
            if f"GFX_ASSET_ROOT_ADD: PASS {settings_root}" not in log:
                raise RuntimeError("settings-backed root registration failed")
            rows = SCAN_RE.findall(log)
            if not rows:
                raise RuntimeError("asset scan trace missing")
            roots, models, textures, guest_models, guest_textures, generated = (
                int(value) for value in rows[-1])
            if roots < 2:
                raise RuntimeError(f"only {roots} guest root(s) indexed")
            if guest_models < 2 or guest_textures < 1:
                raise RuntimeError(
                    f"guest assets missing from the index: "
                    f"models={guest_models} textures={guest_textures}")
            if models <= guest_models:
                raise RuntimeError(
                    f"local resources missing from the index: {models} total "
                    f"vs {guest_models} guest")
            if textures < 1:
                raise RuntimeError("no textures indexed")
            #opencode: the texture hook writes 7 files (5 generators + the
            #   normal bake + the ORM pack); all must join the grid.
            if generated != 7:
                raise RuntimeError(
                    f"generated textures not indexed (generated={generated})")
    except (OSError, RuntimeError, ValueError) as error:
        print(f"GFX_ASSET_ROOTS_CHECK: FAIL — {error}", file=sys.stderr)
        return 1
    print("GFX_ASSET_ROOTS_CHECK: PASS GX_ASSETS and settings-backed browse "
          f"roots indexed ({models} models / {textures} textures, "
          f"{guest_models} guest models, {generated} generated)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

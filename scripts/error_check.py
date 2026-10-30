#!/usr/bin/env python3
#opencode: error-path gate — feed the engine bad inputs headlessly and assert
#   each failure is *reported* (an error trace) without a crash or a silent
#   success. Complements the static error-handling audit.

import json
import sys
import tempfile
from pathlib import Path

import audit_common as audit


ROOT = audit.ROOT
BIN = audit.BIN
PRESET = ROOT / "resources" / "tools" / "gfx_editor.json"


def run_editor(environment, frames=20):
    with audit.settings_preserved():
        result = audit.run_scene("--gfx", environment, width=None, height=None,
            fps=None, frames=frames, timeout=120, check=True,
            extra_args=("--gfx-editor",))
    return audit.strip_ansi(result.stdout or "")


def run_raymarch(environment, frames=20):
    with audit.settings_preserved():
        result = audit.run_scene("--raymarch", environment, width=None,
            height=None, fps=None, frames=frames, timeout=120, check=True)
    return audit.strip_ansi(result.stdout or "")


def main():
    if not BIN.is_file() or not PRESET.is_file():
        print("ERROR_CHECK: missing binary or preset", file=sys.stderr)
        return 1
    try:
        with tempfile.TemporaryDirectory(prefix="error_check_",
                                         dir="/tmp/opencode") as temporary:
            folder = Path(temporary)
            missing = folder / "does_not_exist.json"
            log = run_editor(audit.scene_env({
                "GX_FIXED_DT": "0.0166666667",
                "GX_GFX_PRESET_LOAD": str(missing),
            }, clean=True))
            if ("GFX_PRESET: Load failed: file not found" not in log
                    or "name=not-found" not in log):
                raise RuntimeError(
                    "missing preset produced no not-found error report")
            corrupt = folder / "corrupt.json"
            corrupt.write_text("{ this is not json ")
            log = run_editor(audit.scene_env({
                "GX_FIXED_DT": "0.0166666667",
                "GX_GFX_PRESET_LOAD": str(corrupt),
            }, clean=True))
            if ("GFX_PRESET: Preset load failed" not in log
                    or "name=parse" not in log):
                raise RuntimeError(
                    "corrupt preset produced no parse error report")
            bad_defines = ("#error nope\n"
                "#define CF_WATER 1\n#define CF_OBJECTS 1\n"
                "#define CF_LIGHTS 1\n#define CF_FOG 1\n#define CF_GLOW 1\n"
                "#define CF_AO 1\n#define CF_SKYBOX 0\n#define CF_GRID 0\n"
                "#define CF_DEBUGVIS 0\n#define CF_SDF_REPEAT 0\n"
                "#define CF_SDF_SMOOTH 0\n#define CF_HSV 0\n"
                "#define CF_VEGETATION 1\n#define CF_FBMNOISE3D 0\n"
                "#define CF_NOISE_TEX 1\n#define CF_SKY_PROCEDURAL 1")
            log = run_raymarch(audit.scene_env({
                "GX_FIXED_DT": "0.0166666667",
                "GX_GFX_RAYMARCH_DEFINES": bad_defines,
            }, clean=True))
            if "compile FAILED" not in log or "name=gl" not in log:
                raise RuntimeError(
                    "bad shader produced no gl error code")
            fixture = json.loads(PRESET.read_text())
            fixture["models"][0]["path"] = "resources/model/does_not_exist.glb"
            broken = folder / "broken_model.json"
            broken.write_text(json.dumps(fixture))
            log = run_editor(audit.scene_env({
                "GX_FIXED_DT": "0.0166666667",
                "GX_GFX_PRESET_LOAD": str(broken),
            }, clean=True))
            if "Model load FAILED" not in log or "name=not-found" not in log:
                raise RuntimeError(
                    "missing model produced no not-found code")
    except (OSError, RuntimeError, ValueError) as error:
        print(f"ERROR_CHECK: FAIL — {error}", file=sys.stderr)
        return 1
    print("ERROR_CHECK: PASS missing/corrupt presets, bad shader and missing "
          "model all report codes (no crash, no silent success)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
#ai: postprocess chain gate — the --pp scene renders its test texture, built-in
#   chain presets visibly change the frame (A/B), a bad chain entry reports
#   through the error-code path without taking the module down, and the editor
#   panel draws.

import json
import re
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image

import audit_common as audit

SCENE = "--pp"
BASE_MIN_MEAN = 30.0
BASE_MIN_STD = 10.0
PIXELATION_MIN_DELTA = 5.0
VIGNETTE_MIN_DELTA = 20.0
BADCODE_RE = re.compile(r"postprocess shader '[^']*' code=(\d+) name=([\w-]+)")
BADHOOK_RE = re.compile(r"PP_HOOK: badtype push=(\d+) code=(\d+)")
UI_MARKER = "GFX_PP_UI: panel=1"
PP_CHECK_RE = re.compile(
    r"GFX_PP_CHECK: (PASS|FAIL) save=(\d+) load=(\d+) restored=(\d+) "
    r"chain=(\d+)")


def capture(temp_root, tag, extra, scene=SCENE):
    directory = temp_root / tag
    directory.mkdir(exist_ok=True)
    overrides = {
        "GX_FIXED_DT": "0.0166666667",
        "GX_FRAMES": "30",
    }
    overrides.update(extra)
    environment = audit.scene_env(overrides, clean=True)
    with audit.settings_preserved():
        result = audit.run_scene(scene, environment, width=None, height=None,
                                 fps=None, frames=30, shot_frame=20,
                                 screenshot_dir=directory, timeout=120,
                                 check=True)
    log = audit.strip_ansi(result.stdout or "")
    shot = audit.newest_shot(directory)
    if shot is None:
        raise RuntimeError(f"{tag}: capture missing")
    pixels = np.asarray(Image.open(shot).convert("RGB"), dtype=np.float32)
    return pixels, log


def check_frames(temp_root):
    base, _ = capture(temp_root, "pp_base", {})
    if float(base.mean()) < BASE_MIN_MEAN or float(base.std()) < BASE_MIN_STD:
        raise RuntimeError(
            f"PP scene is blank (mean {base.mean():.1f}, std {base.std():.1f})")
    pixelation, _ = capture(temp_root, "pp_pixelation",
                            {"GX_PP_PRESET": "6"})
    vignette, _ = capture(temp_root, "pp_vignette", {"GX_PP_PRESET": "5"})
    strongblur, _ = capture(temp_root, "pp_strongblur",
                            {"GX_PP_PRESET": "2"})
    pixel_delta = float(np.abs(base - pixelation).mean())
    vignette_delta = float(np.abs(base - vignette).mean())
    strongblur_delta = float(np.abs(base - strongblur).mean())
    if pixel_delta < PIXELATION_MIN_DELTA:
        raise RuntimeError(
            f"pixelation preset has no effect (delta {pixel_delta:.2f})")
    if vignette_delta < VIGNETTE_MIN_DELTA:
        raise RuntimeError(
            f"vignette preset has no effect (delta {vignette_delta:.2f})")
    if strongblur_delta < PIXELATION_MIN_DELTA:
        raise RuntimeError(
            f"strongblur preset has no effect (delta {strongblur_delta:.2f})")
    if float(vignette.mean()) >= float(base.mean()):
        raise RuntimeError("vignette did not darken the frame")
    return pixel_delta, vignette_delta, strongblur_delta


def check_error_path(temp_root):
    _, log = capture(temp_root, "pp_badtype", {"GX_PP_BADTYPE": "1"})
    badcode = BADCODE_RE.search(log)
    badhook = BADHOOK_RE.search(log)
    if not badcode or int(badcode.group(1)) <= 0:
        raise RuntimeError("bad shader did not set an error code")
    if badcode.group(2) != "not-found":
        raise RuntimeError(f"unexpected error name: {badcode.group(2)}")
    if not badhook or int(badhook.group(1)) != 1 or int(badhook.group(2)) <= 0:
        raise RuntimeError("bad-type probe did not fail through the error path")
    return int(badcode.group(1))


def check_scene_view(temp_root):
    """GX_PP_VIEW=1 post-processes a raster scene: a chain preset must change
    the gfx frame (injectCanvas feeds the chain the canvas)."""
    base, _ = capture(temp_root, "pp_view_base", {"GX_PP_VIEW": "1"},
                      scene="--gfx")
    pixelation, _ = capture(temp_root, "pp_view_pixelation",
                            {"GX_PP_VIEW": "1", "GX_PP_PRESET": "6"},
                            scene="--gfx")
    if float(base.mean()) < BASE_MIN_MEAN or float(base.std()) < BASE_MIN_STD:
        raise RuntimeError(
            f"PP view scene is blank (mean {base.mean():.1f}, "
            f"std {base.std():.1f})")
    delta = float(np.abs(base - pixelation).mean())
    if delta < PIXELATION_MIN_DELTA:
        raise RuntimeError(
            f"chain has no effect on the gfx scene (delta {delta:.2f})")
    return delta


def check_roundtrip(temp_root):
    """The headless save-mutate-reload hook restores the chain, inject slots
    and render scale, and the JSON carries the schema sections."""
    folder = temp_root / "pp_roundtrip"
    folder.mkdir(exist_ok=True)
    shot = folder / "shot"
    shot.mkdir(exist_ok=True)
    environment = audit.scene_env({
        "GX_FIXED_DT": "0.0166666667",
        "GX_FRAMES": "30",
        "GX_PP_CHECK": str(folder),
    }, clean=True)
    with audit.settings_preserved():
        result = audit.run_scene("--pp", environment, width=None, height=None,
                                 fps=None, frames=30, shot_frame=20,
                                 screenshot_dir=shot, timeout=120, check=True)
    log = audit.strip_ansi(result.stdout or "")
    marker = PP_CHECK_RE.search(log)
    if not marker or marker.group(1) != "PASS":
        raise RuntimeError(f"round-trip hook failed: {marker.groups() if marker else 'no marker'}")
    preset_path = folder / "pp_roundtrip.json"
    if not preset_path.is_file():
        raise RuntimeError("round-trip preset file missing")
    preset = json.loads(preset_path.read_text())
    problems = []
    shaders = preset.get("shaders", {})
    if int(shaders.get("shadersAmount", -1)) != 1:
        problems.append(f"shadersAmount {shaders.get('shadersAmount')}")
    entry = (shaders.get("shader") or [{}])[0]
    if int(entry.get("loops", 0)) != 3:
        problems.append(f"entry loops {entry.get('loops')}")
    uniforms = entry.get("uniforms") or [{}]
    value = ((uniforms[0] or {}).get("uniform") or {}).get("value") or []
    if len(value) != 4 or abs(float(value[0]) - 0.111) > 1e-3:
        problems.append(f"entry uniform value {value}")
    if int(preset.get("inject", {}).get("amount", -1)) != 2:
        problems.append(f"inject amount {preset.get('inject', {}).get('amount')}")
    render_scale = preset.get("texture", {}).get("renderScale")
    if render_scale is None or abs(float(render_scale) - 0.75) > 1e-3:
        problems.append(f"renderScale {render_scale}")
    if problems:
        raise RuntimeError("preset round-trip mismatch: " + ", ".join(problems))
    return int(marker.group(5))


def check_panel(temp_root):
    directory = temp_root / "panel"
    directory.mkdir(exist_ok=True)
    environment = audit.scene_env({
        "GX_FIXED_DT": "0.0166666667",
        "GX_FRAMES": "20",
    }, clean=True)
    with audit.settings_preserved():
        #opencode: parse order matters — --pp follows --gfx-editor.
        result = audit.run_scene("--gfx-editor", environment, width=None,
                                 height=None, fps=None, frames=20, shot_frame=15,
                                 screenshot_dir=directory, timeout=120,
                                 check=True, extra_args=("--pp",))
    log = audit.strip_ansi(result.stdout or "")
    if UI_MARKER not in log:
        raise RuntimeError("postprocess panel did not draw")
    if audit.newest_shot(directory) is None:
        raise RuntimeError("editor capture missing")


def main():
    if not audit.BIN.is_file():
        print("GFX_PP_CHECK: missing binary", file=sys.stderr)
        return 1
    try:
        with tempfile.TemporaryDirectory(prefix="gfx_pp_",
                                         dir=str(audit.SCRATCH.parent)) as temp:
            temp_root = Path(temp)
            pixel_delta, vignette_delta, strongblur_delta = \
                check_frames(temp_root)
            code = check_error_path(temp_root)
            view_delta = check_scene_view(temp_root)
            chain = check_roundtrip(temp_root)
            check_panel(temp_root)
    except (OSError, RuntimeError, ValueError) as error:
        print(f"GFX_PP_CHECK: FAIL — {error}", file=sys.stderr)
        return 1
    print(f"GFX_PP_CHECK: PASS chain presets change the frame "
          f"(pixelation {pixel_delta:.1f}, vignette {vignette_delta:.1f}, "
          f"strongblur {strongblur_delta:.1f}) "
          f"+ bad shader reports code {code} + gfx-view delta {view_delta:.1f}"
          f" + preset round-trip ({chain} entry) + editor panel")
    return 0


if __name__ == "__main__":
    sys.exit(main())

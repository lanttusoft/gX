#!/usr/bin/env python3
#opencode: scene-camera render gate. An authored camera renders the scene to
#   a PNG (cropped to the docked viewport, no editor overlays); the "cameras"
#   preset section must round-trip through a batch save, two distinct cameras
#   must render different frames, and a camera matching the live view must
#   reproduce the harness screenshot crop within the overlay tolerance.

import argparse
import json
import re
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image

import audit_common as audit


ROOT = audit.ROOT

#opencode: absolute geometry in this gate's assertions — keep the engine size.
audit.keep_engine_size()
BIN = audit.BIN
PRESET = ROOT / "resources" / "tools" / "gfx_editor.json"
QUEUE_RE = re.compile(r"GFX_RENDER_CAMERA: queued index=(\d+)")
RESULT_RE = re.compile(r"GFX_RENDER_CAMERA: (PASS|FAIL) path=(\S+)")
VIEW_CAMERA = {"position": [0.0, 12.0, 0.02], "target": [0.0, 0.0, 0.0],
               "up": [0.0, 1.0, 0.0], "fovy": 50.0, "projection": 0}
OTHER_CAMERA = {"position": [10.0, 3.0, 0.0], "target": [0.0, 0.5, 0.0],
                "up": [0.0, 1.0, 0.0], "fovy": 45.0, "projection": 0}
SHOT_FRAME = 30
#opencode: the reference screenshot carries the editor footer overlay and the
#   temporal passes are not settled at the same frame, so absolute equality is
#   not expected. Calibration: the matching view differs by mean 4.4 while a
#   different camera differs by 65.7, so the band plus the relative contrast
#   (matching view must be at least 4x closer) pin the camera's identity.
REFERENCE_MEAN_TOLERANCE = 10.0
REFERENCE_OUTLIER_FRACTION = 0.12
REFERENCE_CONTRAST_RATIO = 4.0
CAPTURE_PASS_MEAN = 1.0


def make_fixture(path, cameras, view):
    document = json.loads(PRESET.read_text())
    document["camera"] = view
    document["cameras"] = [{"name": name, "description": "", "view": camera}
                           for name, camera in cameras]
    path.write_text(json.dumps(document, indent=2) + "\n")


def run_editor(fixture, extra, shot_dir=None, shot_frame=None):
    overrides = {"GX_FIXED_DT": "0.0166666667", "GX_FRAMES": "40",
                 "GX_GFX_PRESET_LOAD": str(fixture), "GX_DOCK_TRACE": "1"}
    overrides.update(extra)
    environment = audit.scene_env(overrides, clean=True)
    result = audit.run_scene("--gfx", environment, width=None, height=None,
                             fps=None, shot_frame=shot_frame,
                             screenshot_dir=shot_dir, timeout=120,
                             check=False, extra_args=("--gfx-editor",))
    return audit.strip_ansi(result.stdout or ""), result.returncode


def viewport_size(log):
    match = None
    for candidate in re.findall(
            r"GFX_DOCK:.*viewport=\((\d+),(\d+),(\d+),(\d+)\)", log):
        match = candidate
    if not match:
        raise RuntimeError("dock viewport trace missing")
    return int(match[2]), int(match[3])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--calibrate", action="store_true")
    arguments = parser.parse_args()
    if not BIN.is_file() or not PRESET.is_file():
        print("GFX_CAMERA_RENDER_CHECK: missing binary or preset", file=sys.stderr)
        return 1
    try:
        with tempfile.TemporaryDirectory(prefix="gfx_camera_render_",
                                         dir="/tmp/opencode") as temporary:
            temp_root = Path(temporary)
            fixture = temp_root / "cameras.json"
            make_fixture(fixture, [("View match", VIEW_CAMERA),
                                   ("Other", OTHER_CAMERA)], VIEW_CAMERA)
            #opencode: the drag run needs a live view that sees the authored
            #   camera's eye position (projecting a camera from itself is
            #   degenerate), so pull the live view back.
            drag_fixture = temp_root / "cameras_drag.json"
            make_fixture(drag_fixture, [("View match", VIEW_CAMERA),
                                        ("Other", OTHER_CAMERA)],
                {"position": [0.0, 12.0, 16.0], "target": [0.0, 0.0, 0.0],
                 "up": [0.0, 1.0, 0.0], "fovy": 50.0, "projection": 0})
            render_a = temp_root / "camera_a.png"
            render_b = temp_root / "camera_b.png"
            render_sized = temp_root / "camera_sized.png"
            with audit.settings_preserved():
                log_a, code_a = run_editor(fixture, {
                    "GX_GFX_RENDER_CAMERA": str(render_a),
                    "GX_GFX_RENDER_CAMERA_INDEX": "0"})
                log_b, code_b = run_editor(fixture, {
                    "GX_GFX_RENDER_CAMERA": str(render_b),
                    "GX_GFX_RENDER_CAMERA_INDEX": "1"})
                log_sized, code_sized = run_editor(fixture, {
                    "GX_GFX_RENDER_CAMERA": str(render_sized),
                    "GX_GFX_RENDER_CAMERA_INDEX": "0",
                    "GX_GFX_RENDER_CAMERA_SIZE": "320x180"})
                log_drag, code_drag = run_editor(drag_fixture,
                    {"GX_GFX_CAMERA_DRAG_CHECK": "1"})
                shot_dir = temp_root / "reference"
                shot_dir.mkdir()
                log_ref, code_ref = run_editor(fixture, {}, shot_dir=shot_dir,
                                               shot_frame=SHOT_FRAME)
            if (code_a != 0 or code_b != 0 or code_ref != 0 or code_sized != 0
                    or code_drag != 0):
                raise RuntimeError(f"editor runs failed ({code_a}/{code_b}/"
                                   f"{code_ref}/{code_sized}/{code_drag})")
            for log, index in ((log_a, "0"), (log_b, "1")):
                if f"queued index={index}" not in log:
                    raise RuntimeError(f"render request trace missing for "
                                       f"camera {index}")
                results = RESULT_RE.findall(log)
                if not results or results[-1][0] != "PASS":
                    raise RuntimeError(f"render did not report PASS: {results}")
            if not render_a.is_file() or not render_b.is_file():
                raise RuntimeError("render PNGs missing")
            pixels_a = np.asarray(Image.open(render_a).convert("RGB"), dtype=np.float32)
            pixels_b = np.asarray(Image.open(render_b).convert("RGB"), dtype=np.float32)
            width, height = viewport_size(log_a)
            #opencode: the dock box is float; the integer crop can land one
            #   pixel short of the traced size.
            if (abs(pixels_a.shape[1] - width) > 1
                    or abs(pixels_a.shape[0] - height) > 1):
                raise RuntimeError(
                    f"render size {pixels_a.shape[1]}x{pixels_a.shape[0]} != "
                    f"viewport {width}x{height}")
            if float(pixels_a.mean()) < 5.0:
                raise RuntimeError("camera render is black")
            camera_delta = float(np.abs(pixels_a - pixels_b).mean())
            if camera_delta < CAPTURE_PASS_MEAN:
                raise RuntimeError(f"two cameras rendered the same frame "
                                   f"(mean {camera_delta:.3f})")
            #opencode: custom output size — the render is rescaled to the
            #   requested pair and stays non-empty.
            if not render_sized.is_file():
                raise RuntimeError("sized render PNG missing")
            sized = np.asarray(Image.open(render_sized).convert("RGB"),
                               dtype=np.float32)
            if sized.shape[1] != 320 or sized.shape[0] != 180:
                raise RuntimeError(f"sized render {sized.shape[1]}x{sized.shape[0]}"
                                   f" != 320x180")
            if float(sized.mean()) < 5.0:
                raise RuntimeError("sized render is black")
            #opencode: camera marker drag — the position and target markers
            #   are pickable and their writes land (position drag keeps the
            #   target; target drag moves only the target).
            if "GFX_CAMERA_DRAG_CHECK: PASS" not in log_drag:
                raise RuntimeError("camera marker drag did not pass")

            reference = None
            shots = sorted((shot_dir).glob("gXss_*.png"))
            if not shots:
                raise RuntimeError("reference screenshot missing")
            frame = np.asarray(Image.open(shots[-1]).convert("RGB"), dtype=np.float32)
            x, y = (int(value) for value in
                    re.findall(r"GFX_DOCK:.*viewport=\((\d+),(\d+),(\d+),(\d+)\)",
                               log_ref)[-1][:2])
            reference = frame[y:y + pixels_a.shape[0], x:x + pixels_a.shape[1]]
            if reference.shape != pixels_a.shape:
                raise RuntimeError(f"reference crop {reference.shape} != render "
                                   f"{pixels_a.shape}")
            reference_delta = np.abs(reference - pixels_a)
            reference_mean = float(reference_delta.mean())
            outlier_fraction = float((reference_delta.mean(axis=2) > 24.0).mean())
            if arguments.calibrate:
                print(f"  CAL camera render: view-vs-other mean={camera_delta:7.3f} "
                      f"reference mean={reference_mean:6.3f} "
                      f"outliers={outlier_fraction:5.3f}")
                return 0
            if (reference_mean > REFERENCE_MEAN_TOLERANCE
                    or outlier_fraction > REFERENCE_OUTLIER_FRACTION
                    or reference_mean * REFERENCE_CONTRAST_RATIO > camera_delta):
                raise RuntimeError(
                    f"camera render differs from the live-view screenshot "
                    f"(mean {reference_mean:.3f}, outliers {outlier_fraction:.3f}, "
                    f"other camera {camera_delta:.3f})")

            #opencode: data-level round trip — a batch save keeps the section.
            saved = temp_root / "saved.json"
            manifest = temp_root / "batch.tsv"
            manifest.write_text(f"{fixture}\t{saved}\n")
            with audit.settings_preserved():
                environment = audit.scene_env(
                    {"GX_FRAMES": "3", "GX_GFX_PRESET_BATCH": str(manifest)},
                    clean=True)
                environment.pop("GX_GFX_PRESET_LOAD", None)
                environment.pop("GX_GFX_PRESET_SAVE", None)
                batch = audit.run_scene("--gfx", environment, width=None,
                                        height=None, fps=None, timeout=120,
                                        check=False,
                                        extra_args=("--gfx-editor",))
            if batch.returncode != 0:
                raise RuntimeError("batch save failed")
            saved_cameras = json.loads(saved.read_text()).get("cameras")
            if not saved_cameras or len(saved_cameras) != 2:
                raise RuntimeError(f"cameras section lost on save: "
                                   f"{saved_cameras}")
            for index, expected in enumerate((VIEW_CAMERA, OTHER_CAMERA)):
                position = saved_cameras[index]["view"]["position"]
                if any(abs(a - b) > 1e-4 for a, b in zip(position,
                                                         expected["position"])):
                    raise RuntimeError(f"camera {index} position changed")
    except (OSError, RuntimeError, ValueError) as error:
        print(f"GFX_CAMERA_RENDER_CHECK: FAIL — {error}", file=sys.stderr)
        return 1
    print(f"GFX_CAMERA_RENDER_CHECK: PASS camera render {width}x{height} "
          f"(view-vs-other {camera_delta:.2f}, reference {reference_mean:.2f}), "
          "sized 320x180, marker drag, section round-trips")
    return 0


if __name__ == "__main__":
    sys.exit(main())

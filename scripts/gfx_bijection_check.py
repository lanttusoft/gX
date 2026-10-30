#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: bijection gate — an operation followed by its inverse must return
#   the exact starting state. Two families:
#
#   1. Serialization fixed point (no rendering): save(load(x)) must equal
#      save(load(save(load(x)))) for the editor preset and a schema probe. A
#      field the loader or serializer drops, reorders, or normalizes
#      asymmetrically breaks the equality even when authored values survive
#      (gfx_preset_check covers the authored-value direction).
#   2. Effect on->off residue: a frame captured after the effect ran for a
#      while and was then disabled must equal a frame from a run that never
#      enabled it, while a run that keeps it enabled must differ (the
#      negative control proving the effect is actually visible). Catches
#      history/RT state that survives a toggle. Mid-run flips use the
#      editor's GX_GFX_STATE_SCRIPT hook ("frame:FLAG=value;...").
#
#   Usage: DISPLAY=:1 python3 scripts/gfx_bijection_check.py
#          python3 scripts/gfx_bijection_check.py --calibrate  (print diffs)
#   GPU/display required — use `make gfx-bijection-check`.

import argparse
import copy
import json
import os
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
SOURCE_PRESET = ROOT / "resources" / "tools" / "gfx_editor.json"
IGNORED_KEYS = {"name", "description", "created_date", "created_time"}
CAPTURE_FRAME = 150
TOGGLE_FRAME = 90
#opencode: residue must be invisible; the visibility floor proves the effect
#   is actually exercised (the negative control). Calibrated 2026-09-27 with
#   --calibrate: every toggle measured residue 0.000/0.0 (bit-identical), so
#   the small tolerances are pure safety margin.
RESIDUE_MAX = 2
RESIDUE_MEAN = 0.02
VISIBLE_MEAN_DEFAULT = 1.0
#opencode: AA-only toggles move fewer pixels than full-screen effects; both
#   floors keep at least a 2x margin over the calibration.
#opencode: re-baselined 2026-10-14 — the TAA calibration (0.915) was inflated
#   by a state-leak artifact: the resolve's `prevBlendTAA` was captured after
#   the resolve had already disabled blending, so TAA-on frames kept blending
#   off for the rest of the frame (a large frame delta vs TAA-off, not AA).
#   The camera-only temporal landing fixed the save/restore placement
#   (bisected: the drop is exactly that fix). The genuine TAA visibility
#   measures 0.38-0.42 (0.382 clean HEAD, 0.416 with the current tree), so the
#   floor is 0.2 — the file's 2x-margin rule.
VISIBLE_MEAN = {
    "ENABLE_TAA": 0.2,
    "ENABLE_PLANAR_REFLECTIONS": 0.2,
    "MODEL0_PLANAR_REFLECTION": 0.2,
}
#opencode: the undo round trip is only meaningful if the drag visibly moved
#   the model; the calibration measured 14.6, so 1.0 is a wide floor.
VISIBLE_MEAN_DRAG = 1.0


#opencode: toggle rows — flag name plus the fixture preparation that makes
#   the effect visible. The MODEL0_* row is the editor's make/unmake mirror
#   pair at model level (the global planar gate stays on for both runs).
EFFECT_ROWS = (
    ("ENABLE_TAA", None),
    ("ENABLE_SSR", "receiveSSR"),
    ("ENABLE_PLANAR_REFLECTIONS", "planar"),
    ("ENABLE_BLOOM", None),
    ("ENABLE_SSAO", None),
    ("ENABLE_SHADOWS", "shadows"),
    ("MODEL0_PLANAR_REFLECTION", "mirror"),
)

#opencode: the viewport drag and Ctrl+Z/Ctrl+Y keys match gfx-picker-check, so
#   the same calibrated gesture drives this pixel round trip.
UNDO_ACTIONS = "1.5 move 751 381 200;1.75 drag 751 381 900 357 500"
UNDO_FRAME = 260
UNDO_FRAMES = 300

#opencode: every other effect stays off so a toggle's residue is attributable.
BASE_STATE = {
    "ENABLE_TAA": False,
    "ENABLE_FXAA": False,
    "ENABLE_SSR": False,
    "ENABLE_SSR_AUXVIEW": False,
    "ENABLE_PLANAR_REFLECTIONS": False,
    "ENABLE_REFLECTIONS": False,
    "ENABLE_BLOOM": False,
    "ENABLE_SSAO": False,
    "ENABLE_SHADOWS": False,
    "ENABLE_POINT_SHADOWS": False,
    "ENABLE_FOG": False,
    "ENABLE_VOLUME_FOG": False,
    "ENABLE_PARTICLES": False,
    "ENABLE_MOTION_BLUR": False,
    "ENABLE_DOF": False,
    "ENABLE_WATER": False,
    "ENABLE_COOKIES": False,
    "ENABLE_GODRAYS": False,
    "ENABLE_LENS_FLARE": False,
    "ENABLE_LIGHTING_AMBIENT": True,
    "ENABLE_LIGHTING_DIFFUSE": True,
    "ENABLE_LIGHTING_SPECULAR": True,
    "SHOW_LIGHT_MARKERS": False,
}


#opencode: the probe carries non-default values through nested heaps so the
#   fixed point exercises more than the shipped editor defaults.
def make_fixture(path, flag, prep, enabled):
    document = json.loads(SOURCE_PRESET.read_text())
    state = document["state"]
    state.update(BASE_STATE)
    if prep == "mirror":
        state["ENABLE_PLANAR_REFLECTIONS"] = True
        document["models"][0]["params"]["planarReflection"] = bool(enabled)
    else:
        state[flag] = bool(enabled)
        if prep == "receiveSSR":
            document["models"][0]["params"]["receiveSSR"] = True
        if prep == "planar":
            document["models"][0]["params"]["planarReflection"] = True
        if prep == "shadows":
            document["lights"][0]["castShadows"] = True
    path.write_text(json.dumps(document, indent=2) + "\n")


def make_probe(path):
    document = json.loads(SOURCE_PRESET.read_text())
    document["state"].update({
        "exposure": 1.37,
        "parallaxOffset": 0.0175,
        "normalYThreshold": 0.23,
        "ENABLE_WATER": False,
        "ENABLE_PLANAR_REFLECTIONS": False,
    })
    document["ssr"].update({
        "normalBufferEnabled": True,
        "maxDepthDelta": 0.01234,
        "worldGapThreshold": 1.75,
    })
    document["bloom"].update({"knee": 0.27})
    path.write_text(json.dumps(document, indent=2) + "\n")


#opencode: fixed point — the second round trip must reproduce the first save.
def check_serialization_fixed_point(temp_root):
    presets = [("editor", SOURCE_PRESET)]
    probe = temp_root / "probe.json"
    make_probe(probe)
    presets.append(("probe", probe))
    manifestLines = []
    for name, source in presets:
        saved1 = temp_root / f"rt1_{name}.json"
        saved2 = temp_root / f"rt2_{name}.json"
        for stale in (saved1, saved2):
            if stale.is_file():
                stale.unlink()
        manifestLines.append(f"{source}\t{saved1}")
        manifestLines.append(f"{saved1}\t{saved2}")
    manifest = temp_root / "fixed_point.tsv"
    manifest.write_text("\n".join(manifestLines) + "\n")

    environment = audit.scene_env({"GX_FRAMES": "3", "GX_GFX_PRESET_BATCH":
                                   str(manifest)}, clean=True)
    environment.pop("GX_GFX_PRESET_LOAD", None)
    environment.pop("GX_GFX_PRESET_SAVE", None)
    result = audit.run_scene("--gfx", environment, width=None, height=None,
                             fps=None, timeout=180, check=False,
                             extra_args=("--gfx-editor",))
    log = audit.strip_ansi(result.stdout or "")
    if result.returncode != 0:
        raise RuntimeError(f"fixed point: batch failed ({result.returncode})")
    expected = len(manifestLines)
    for token in ("[GFX] Load done", "[GFX] Save done"):
        if log.count(token) != expected:
            raise RuntimeError(
                f"fixed point: expected {expected} '{token}' entries, "
                f"got {log.count(token)}")
    rows = []
    for name, source in presets:
        saved1 = temp_root / f"rt1_{name}.json"
        saved2 = temp_root / f"rt2_{name}.json"
        if not saved1.is_file() or not saved2.is_file():
            raise RuntimeError(f"{name}: round-trip files missing")
        rows.append((name, json.loads(saved1.read_text()),
                     json.loads(saved2.read_text())))
    return rows


def compare_fixed_point(rows):
    problems = []

    def walk(path, first, second):
        if isinstance(first, dict):
            if not isinstance(second, dict):
                problems.append(f"{path}: {type(first).__name__} -> "
                                f"{type(second).__name__}")
                return
            for key in sorted(set(first) | set(second)):
                if key in IGNORED_KEYS:
                    continue
                if key not in first or key not in second:
                    problems.append(f"{path}.{key}: key present in only one save")
                    continue
                walk(f"{path}.{key}", first[key], second[key])
        elif isinstance(first, list):
            if not isinstance(second, list) or len(first) != len(second):
                problems.append(f"{path}: list shape {len(first)} vs "
                                f"{len(second) if isinstance(second, list) else second}")
                return
            for index, (left, right) in enumerate(zip(first, second)):
                walk(f"{path}[{index}]", left, right)
        elif isinstance(first, bool) or isinstance(second, bool):
            if first is not second:
                problems.append(f"{path}: {first!r} -> {second!r}")
        elif isinstance(first, (int, float)) and isinstance(second, (int, float)):
            if abs(float(first) - float(second)) > 1e-6:
                problems.append(f"{path}: {first!r} -> {second!r}")
        elif first != second:
            problems.append(f"{path}: {first!r} -> {second!r}")

    for name, first, second in rows:
        walk(name, first, second)
    return problems


def capture(temp_root, tag, preset=None, script=None, frame=CAPTURE_FRAME,
            actions=None, frames=None, env_extra=None):
    directory = temp_root / tag
    directory.mkdir(exist_ok=True)
    overrides = {
        "GX_FIXED_DT": "0.0166666667",
        "GX_DOCK_TRACE": "1",
    }
    if preset is not None:
        overrides["GX_GFX_PRESET_LOAD"] = str(preset)
    if script:
        overrides["GX_GFX_STATE_SCRIPT"] = script
    if actions:
        overrides["GX_AI_INPUT"] = actions
    if frames:
        overrides["GX_FRAMES"] = str(frames)
    if env_extra:
        overrides.update(env_extra)
    environment = audit.scene_env(overrides, clean=True)
    result = audit.run_scene("--gfx", environment, width=None, height=None,
                             fps=None, shot_frame=frame,
                             screenshot_dir=directory, timeout=180,
                             check=True, extra_args=("--gfx-editor",))
    shot = audit.newest_shot(directory)
    pixels = np.asarray(Image.open(shot).convert("RGB"), dtype=np.float32)
    log = audit.strip_ansi(result.stdout or "")
    x, y, w, h = audit.dock_viewport(log, pixels.shape[1], pixels.shape[0])
    return pixels[y:y + h, x:x + w]


def effect_diff(before, after):
    delta = np.abs(before - after)
    return float(delta.mean()), float(delta.max())


def run_effect(temp_root, flag, prep):
    off = temp_root / f"{flag}_off.json"
    on = temp_root / f"{flag}_on.json"
    make_fixture(off, flag, prep, False)
    make_fixture(on, flag, prep, True)
    script = f"{TOGGLE_FRAME}:{flag}=0"
    never = capture(temp_root, f"{flag}_never", off, None)
    toggled = capture(temp_root, f"{flag}_toggled", on, script)
    kept = capture(temp_root, f"{flag}_kept", on, None)
    residue_mean, residue_max = effect_diff(never, toggled)
    visible_mean, _ = effect_diff(never, kept)
    return residue_mean, residue_max, visible_mean


#opencode: drag + undo must land on a never-dragged frame and redo must land
#   on the drag frame; the moved frame must differ or the round trip is vacuous.
def check_undo_bijection(temp_root):
    baseline = capture(temp_root, "undo_baseline", frame=UNDO_FRAME,
                       frames=UNDO_FRAMES,
                       actions="1.5 move 751 381 200;1.75 move 900 357 500")
    dragged = capture(temp_root, "undo_dragged", frame=UNDO_FRAME,
                      frames=UNDO_FRAMES, actions=UNDO_ACTIONS)
    undone = capture(temp_root, "undo_undone", frame=UNDO_FRAME,
                     frames=UNDO_FRAMES,
                     actions=UNDO_ACTIONS + ";2.5 key 90 0 1")
    redone = capture(temp_root, "undo_redone", frame=UNDO_FRAME,
                     frames=UNDO_FRAMES,
                     actions=UNDO_ACTIONS + ";2.5 key 90 0 1;3.1 key 89 0 1")
    undo_mean, undo_max = effect_diff(baseline, undone)
    redo_mean, redo_max = effect_diff(dragged, redone)
    moved_mean, _ = effect_diff(baseline, dragged)
    return undo_mean, undo_max, redo_mean, redo_max, moved_mean


#opencode: the snapshot smoke hook perturbs and restores model, camera, and
#   renderer state — the rendered frame must be untouched by the exercise.
def check_snapshot_bijection(temp_root):
    plain = capture(temp_root, "snapshot_plain", SOURCE_PRESET, None,
                    frame=100, frames=120)
    hooked = capture(temp_root, "snapshot_hooked", SOURCE_PRESET, None,
                     frame=100, frames=120,
                     env_extra={"GX_GFX_SNAPSHOT_SMOKE": "1"})
    return effect_diff(plain, hooked)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--calibrate", action="store_true",
                        help="print toggle diffs instead of asserting")
    arguments = parser.parse_args()
    if not BIN.is_file() or not SOURCE_PRESET.is_file():
        print("GFX_BIJECTION_CHECK: missing binary or preset", file=sys.stderr)
        return 1
    try:
        with tempfile.TemporaryDirectory(prefix="gfx_bijection_",
                                         dir="/tmp/opencode") as directory:
            temp_root = Path(directory)
            with audit.settings_preserved():
                rows = check_serialization_fixed_point(temp_root)
            problems = compare_fixed_point(rows)
            if problems:
                raise RuntimeError(
                    "serialization fixed point broken: "
                    + "; ".join(problems[:6]))
            print("GFX_BIJECTION_CHECK: preset save/load fixed point holds "
                  f"({len(rows)} documents)")

            failures = []
            for flag, prep in EFFECT_ROWS:
                with audit.settings_preserved():
                    residue_mean, residue_max, visible_mean = run_effect(
                        temp_root, flag, prep)
                if arguments.calibrate:
                    print(f"  CAL {flag:<28} residue mean={residue_mean:7.3f} "
                          f"max={residue_max:6.1f} visible mean={visible_mean:7.3f}")
                    continue
                residue_ok = (residue_mean <= RESIDUE_MEAN
                              and residue_max <= RESIDUE_MAX)
                visible_ok = (visible_mean
                              >= VISIBLE_MEAN.get(flag, VISIBLE_MEAN_DEFAULT))
                status = "PASS" if (residue_ok and visible_ok) else "FAIL"
                print(f"  {flag:<28} residue mean={residue_mean:7.3f} "
                      f"max={residue_max:6.1f} visible mean={visible_mean:7.3f} "
                      f"{status}")
                if not residue_ok:
                    failures.append(f"{flag}: residue mean={residue_mean:.3f} "
                                    f"max={residue_max:.1f}")
                if not visible_ok:
                    failures.append(f"{flag}: effect not visible "
                                    f"(mean={visible_mean:.3f})")
            with audit.settings_preserved():
                undo_mean, undo_max, redo_mean, redo_max, moved_mean = (
                    check_undo_bijection(temp_root))
            if arguments.calibrate:
                print(f"  CAL undo       vs baseline mean={undo_mean:7.3f} "
                      f"max={undo_max:6.1f}; redo vs drag mean={redo_mean:7.3f} "
                      f"max={redo_max:6.1f}; moved mean={moved_mean:7.3f}")
            else:
                undo_ok = (undo_mean <= RESIDUE_MEAN
                           and undo_max <= RESIDUE_MAX)
                redo_ok = (redo_mean <= RESIDUE_MEAN
                           and redo_max <= RESIDUE_MAX)
                moved_ok = moved_mean >= VISIBLE_MEAN_DRAG
                status = "PASS" if (undo_ok and redo_ok and moved_ok) else "FAIL"
                print(f"  undo/redo round trip        undo mean={undo_mean:7.3f} "
                      f"max={undo_max:6.1f} redo mean={redo_mean:7.3f} "
                      f"max={redo_max:6.1f} moved mean={moved_mean:7.3f} "
                      f"{status}")
                if not undo_ok:
                    failures.append(f"undo residue mean={undo_mean:.3f} "
                                    f"max={undo_max:.1f}")
                if not redo_ok:
                    failures.append(f"redo residue mean={redo_mean:.3f} "
                                    f"max={redo_max:.1f}")
                if not moved_ok:
                    failures.append(f"drag not visible (mean={moved_mean:.3f})")

            with audit.settings_preserved():
                snapshot_mean, snapshot_max = check_snapshot_bijection(
                    temp_root)
            if arguments.calibrate:
                print(f"  CAL snapshot   plain vs hook mean={snapshot_mean:7.3f} "
                      f"max={snapshot_max:6.1f}")
            else:
                snapshot_ok = (snapshot_mean <= RESIDUE_MEAN
                               and snapshot_max <= RESIDUE_MAX)
                status = "PASS" if snapshot_ok else "FAIL"
                print(f"  snapshot round trip         mean={snapshot_mean:7.3f} "
                      f"max={snapshot_max:6.1f} {status}")
                if not snapshot_ok:
                    failures.append(f"snapshot residue mean={snapshot_mean:.3f} "
                                    f"max={snapshot_max:.1f}")

            if arguments.calibrate:
                return 0
            if failures:
                raise RuntimeError("; ".join(failures[:5]))
    except (OSError, RuntimeError, ValueError) as error:
        print(f"GFX_BIJECTION_CHECK: FAIL — {error}", file=sys.stderr)
        return 1
    print(f"GFX_BIJECTION_CHECK: PASS ({len(EFFECT_ROWS)} effect toggles, "
          "undo/redo, and snapshot round trips leave no residue; each effect "
          "visibly changes the frame)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

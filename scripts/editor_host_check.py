#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: global editor host pin — the toolbar is always summonable (F9) and
#   renders the application menu bar in any scene, not only while an editor is
#   open. The sky editor is the pilot panel. F9 is raylib KEY_F9 = 298.

import re
import shutil
import sys
import tempfile
from pathlib import Path

import numpy as np

import audit_common as audit


OUT = audit.SCRATCH / "editor_host_check"
WIDTH = 1024
HEIGHT = 768
TOP = 46  # menu bar + workspace tab row
TAB_RE = re.compile(r'EDITOR_HOST: tab=Sky x=(\d+) y=(\d+) w=(\d+) h=(\d+)')
TAB_ALL_RE = re.compile(r'EDITOR_HOST: tab=(\S+) x=(\d+) y=(\d+) w=(\d+) h=(\d+)')
DOCK_RE = re.compile(r'GFX_DOCK:.*viewport=\(([-0-9.]+),([-0-9.]+),([-0-9.]+),([-0-9.]+)\)')
PANELS_RE = re.compile(r'GFX_DOCK:.*panels=(\d)/(\d)')
UI_RE = re.compile(
    r'GFX_UI_(LAB|SECTION): level=(\w+) name=(\S+) min=(\w+) visible=([01])')
UI_RANK = {"Basic": 0, "Advanced": 1, "Expert": 2}
UI_ALL_LABS = {"Models", "Sky", "World", "FX", "Lights", "Render", "Water",
               "Ray", "Particles", "Refl", "Debug"}
UI_LABS = {
    "Basic": {"Models", "Sky", "World", "FX", "Lights", "Render"},
    "Advanced": {"Models", "Sky", "World", "FX", "Lights", "Render",
                 "Water", "Particles", "Refl"},
    "Expert": UI_ALL_LABS,
}
UI_SECTIONS = {
    "Basic": {
        "visible": {"quality", "look", "ssao", "bloom"},
        "hidden": {"shader_tuning", "gl_level", "render_toggles",
                   "texture_tools", "quality_shader", "quality_aux",
                   "ssao_kernel", "ssgi", "map_tuning", "material_overrides",
                   "profiler", "watches", "sky_lab", "decals", "fog_volume",
                   "godrays", "lensflare", "csm", "bloom_shape",
                   "bloom_auto_exposure", "look_detail", "look_extras",
                   "look_gpu"},
    },
    "Advanced": {
        "visible": {"quality", "look", "ssao", "bloom",
                    "render_toggles", "texture_tools", "quality_shader",
                    "ssao_kernel", "material_overrides", "profiler",
                    "sky_lab", "decals", "fog_volume", "godrays",
                    "lensflare", "csm", "bloom_shape", "look_detail",
                    "look_extras", "look_gpu"},
        "hidden": {"shader_tuning", "gl_level", "quality_aux", "ssgi",
                   "map_tuning", "watches", "bloom_auto_exposure"},
    },
    "Expert": {
        "visible": {"shader_tuning", "gl_level", "quality_aux", "ssgi",
                    "map_tuning", "watches", "bloom_auto_exposure"},
        "hidden": set(),
    },
}


#ai: one runtime pin for the Basic/Advanced/Expert editor surface — every
#   traced row must match its minimum level, and the lab/section sets must
#   stay complete (no silently ungated panel slipping back into Basic).
def check_ui_level(tag, level, overrides):
    environment = dict(overrides)
    environment.update({
        "GX_UI_LEVEL": level.lower(),
        "GX_UI_TRACE": "1",
        "GX_GFX_PANEL_LABS": str((1 << 11) - 1),
    })
    code, log, _ = run(tag, "--gfx", environment, frames=30, shot=20)
    if code != 0:
        raise RuntimeError(f"UI level {level} run failed ({code})")
    rows = {}
    for kind, rowLevel, name, minimum, visible in UI_RE.findall(log):
        if rowLevel != level:
            continue
        rows[(kind, name)] = (minimum, visible == "1")
    if not rows:
        raise RuntimeError(f"UI level {level}: GFX_UI trace missing")
    for (kind, name), (minimum, visible) in rows.items():
        expected = UI_RANK[level] >= UI_RANK[minimum]
        if visible != expected:
            raise RuntimeError(
                f"UI level {level}: {kind} {name} min={minimum} "
                f"visible={int(visible)}, expected {int(expected)}")
    labs = {name for (kind, name) in rows if kind == "LAB"}
    if labs != UI_ALL_LABS:
        raise RuntimeError(
            f"UI level {level}: lab coverage {sorted(labs)}")
    visibleLabs = {name for (kind, name), (_, visible) in rows.items()
                   if kind == "LAB" and visible}
    if visibleLabs != UI_LABS[level]:
        raise RuntimeError(
            f"UI level {level}: visible labs {sorted(visibleLabs)}")
    sections = {name for (kind, name) in rows if kind == "SECTION"}
    expected = UI_SECTIONS[level]
    missingVisible = expected["visible"] - sections
    missingHidden = expected["hidden"] - sections
    if missingVisible or missingHidden:
        raise RuntimeError(
            f"UI level {level}: section trace missing "
            f"visible={sorted(missingVisible)} hidden={sorted(missingHidden)}")
    for (kind, name), (_, visible) in rows.items():
        if kind != "SECTION":
            continue
        if name in expected["hidden"] and visible:
            raise RuntimeError(
                f"UI level {level}: section {name} leaked into the surface")
        if name in expected["visible"] and not visible:
            raise RuntimeError(
                f"UI level {level}: section {name} missing from the surface")
    return len(rows)


#ai: virtual-screen presentation — the scene renders at 320x240 while the
#   window stays 1024x768; Soft (linear) must reduce pixel-level blockiness
#   against Sharp (nearest), and the trace must report the split.
CONSOLE_RE = re.compile(
    r"MAIN: virtual=(\d+)x(\d+) window=(\d+)x(\d+) "
    r"render=(\d+)x(\d+) filter=(\d)")


def check_console_240p(tag, mode):
    code, log, image = run(tag, "--gfx", {
        "GX_VIRTUAL_WIDTH": "320",
        "GX_VIRTUAL_HEIGHT": "240",
        "GX_PRESENT_FILTER": mode,
        "GX_WINDOW_TRACE": "1",
    }, frames=30, shot=20)
    if code != 0:
        raise RuntimeError(f"console {mode} run failed ({code})")
    match = CONSOLE_RE.search(log)
    if not match:
        raise RuntimeError(f"console {mode}: virtual-screen trace missing")
    values = tuple(int(value) for value in match.groups())
    expected = (320, 240, WIDTH, HEIGHT, 320, 240, 1 if mode == "soft" else 0)
    if values != expected:
        raise RuntimeError(
            f"console {mode}: trace {values}, expected {expected}")
    if image is None or image.shape[:2] != (HEIGHT, WIDTH):
        raise RuntimeError(
            f"console {mode}: capture {None if image is None else image.shape}")
    gray = image.mean(axis=2)
    crop = gray[100:-100, 100:-100]
    laplacian = np.abs(4.0 * crop[1:-1, 1:-1] - crop[:-2, 1:-1]
        - crop[2:, 1:-1] - crop[1:-1, :-2] - crop[1:-1, 2:])
    return float(laplacian.mean())


#ai: Console Max — full feature closure + ultra quality at 320x240 must beat
#   the same preset at 1024x768 (relative, host-independent); the absolute
#   frame time is reported, not hard-gated.
MAX_PLAN_RE = re.compile(
    r"GFX_PLAN mask=0x([0-9a-fA-F]+) count=(\d+) needHdr=(\d+) "
    r"effects=(\d+) missing=0x([0-9a-fA-F]+)")
MAX_PROFILE_RE = re.compile(
    r"GX_PROFILE: frame=(\d+) total_ms=([\d.]+) min_ms=([\d.]+) "
    r"avg_ms=([\d.]+) max_ms=([\d.]+)")


def check_console_max(tag, virtual):
    environment = {
        "GX_FIXED_DT": "0.0166666667",
        "GX_FRAMES": "240",
        "GX_CONSOLE_MAX": "1",
        "GX_BENCHMARK_UNCAPPED": "1",
        "GX_GFX_PLAN": "1",
        "GX_WINDOW_TRACE": "1",
        "GX_PROFILE_DUMP": "120",
        "GX_PRESENT_FILTER": "soft",
    }
    if virtual:
        environment["GX_VIRTUAL_WIDTH"] = "320"
        environment["GX_VIRTUAL_HEIGHT"] = "240"
    env = audit.scene_env(environment, clean=True)
    result = audit.run_scene("--gfx", env, width=WIDTH, height=HEIGHT,
        fps=None, frames=240, screenshot_dir=None, timeout=180, check=False)
    if result.returncode != 0:
        raise RuntimeError(f"console max {tag} failed ({result.returncode})")
    log = audit.strip_ansi(result.stdout or "")
    plan = MAX_PLAN_RE.search(log)
    if not plan:
        raise RuntimeError(f"console max {tag}: GFX_PLAN missing")
    mask, count, needHdr, effects, missing = plan.groups()
    if int(mask, 16) & 0x1FFFF != 0x1FFFF:
        raise RuntimeError(f"console max {tag}: feature mask 0x{mask}")
    if int(count) < 17 or needHdr != "1" or effects != "1" \
            or int(missing, 16) != 0:
        raise RuntimeError(
            f"console max {tag}: plan count={count} hdr={needHdr} "
            f"effects={effects} missing=0x{missing}")
    for feature in ("ssgi", "planar-reflections", "volume-fog", "dof",
                    "taa", "fxaa"):
        if f"GFX_PLAN feature={feature} " not in log:
            raise RuntimeError(f"console max {tag}: {feature} not enabled")
    trace = CONSOLE_RE.search(log)
    expected = ((320, 240, WIDTH, HEIGHT, 320, 240, 1) if virtual
                else (WIDTH, HEIGHT, WIDTH, HEIGHT, WIDTH, HEIGHT, 1))
    if not trace or tuple(int(value) for value in trace.groups()) != expected:
        raise RuntimeError(
            f"console max {tag}: trace "
            f"{trace.groups() if trace else None}, expected {expected}")
    profiles = MAX_PROFILE_RE.findall(log)
    if not profiles:
        raise RuntimeError(f"console max {tag}: profile missing")
    frame, total, minimum, average, maximum = profiles[-1]
    if int(frame) < 240:
        raise RuntimeError(f"console max {tag}: early profile frame {frame}")
    return float(average)


def run(tag, scene, extra, frames=30, shot=20):
    directory = OUT / tag
    directory.mkdir(parents=True, exist_ok=True)
    overrides = {
        "GX_FIXED_DT": "0.0166666667",
        "GX_FRAMES": str(frames),
        "GX_NO_SETTINGS_SAVE": "1",
    }
    overrides.update(extra)
    environment = audit.scene_env(overrides, clean=True)
    result = audit.run_scene(scene, environment, width=WIDTH, height=HEIGHT,
        fps=None, frames=frames, shot_frame=shot, screenshot_dir=directory,
        timeout=150, check=False)
    log = audit.strip_ansi(result.stdout or "")
    image = None
    shot_file = audit.newest_shot(directory)
    if shot_file is not None:
        image = np.asarray(audit.Image.open(shot_file).convert("RGB"),
                           dtype=np.float32)
    return result.returncode, log, image


def top_band(image):
    return image[:TOP, :, :]


def main():
    if not audit.BIN.is_file():
        print("EDITOR_HOST_CHECK: FAIL — missing binary", file=sys.stderr)
        return 1
    try:
        base_code, base_log, base = run("base", "--title", {})
        host_code, host_log, host = run("host", "--title",
            {"GX_EDITOR_HOST": "1"})
        if base_code != 0 or host_code != 0:
            raise RuntimeError(f"title runs failed ({base_code}/{host_code})")
        if "EDITOR_HOST: open=1" not in host_log:
            raise RuntimeError("host env did not open the toolbar")
        if base is None or host is None:
            raise RuntimeError("title captures missing")
        delta = float(np.abs(top_band(base) - top_band(host)).mean())
        if delta < 5.0:
            raise RuntimeError(
                f"toolbar bar not visible in the title (top delta {delta:.2f})")

        toggle_code, toggle_log, _ = run("toggle", "--title", {
            "GX_AI_INPUT": "1.0 key 298;2.0 key 298",
        }, frames=200, shot=150)
        if toggle_code != 0:
            raise RuntimeError(f"F9 toggle run failed ({toggle_code})")
        if toggle_log.count("EDITOR_HOST: open=1") < 1:
            raise RuntimeError("F9 did not open the toolbar")
        if toggle_log.count("EDITOR_HOST: open=0") < 1:
            raise RuntimeError("F9 did not close the toolbar")
        closed = audit.newest_shot(OUT / "toggle")
        if closed is None:
            raise RuntimeError("F9 toggle capture missing")
        closed_img = np.asarray(audit.Image.open(closed).convert("RGB"),
                                dtype=np.float32)
        closed_delta = float(np.abs(top_band(base) - top_band(closed_img)).mean())
        if closed_delta > 3.0:
            raise RuntimeError(
                f"toolbar did not hide on the second F9 (delta {closed_delta:.2f})")

        gfx_code, gfx_log, _ = run("gfx", "--gfx", {"GX_EDITOR_HOST": "1"},
                                   frames=30, shot=20)
        if gfx_code != 0 or "EDITOR_HOST: open=1" not in gfx_log:
            raise RuntimeError("host did not open in the gfx scene")

        sky_code, sky_log, sky = run("sky", "--title",
            {"GX_EDITOR_HOST": "1", "GX_EDITOR_WORKSPACE": "Sky"},
            frames=90, shot=70)
        if sky_code != 0:
            raise RuntimeError(f"sky host run failed ({sky_code})")
        if "EDITOR_HOST: open=1" not in sky_log:
            raise RuntimeError("host missing in the sky run")
        if "SKY_EDITOR_UI: panel=1" not in sky_log:
            raise RuntimeError("sky panel did not open")
        if "EDITOR_HOST: active=Sky" not in sky_log:
            raise RuntimeError("editor registry did not report Sky as active")
        if "EDITOR_HOST: workspace=Sky" not in sky_log:
            raise RuntimeError("Sky workspace was not applied")
        if sky is None:
            raise RuntimeError("sky capture missing")
        #opencode: the sky scene opens its own (wider) window; compare the
        #   toolbar band on the common left edge.
        if sky.shape[1] < WIDTH:
            raise RuntimeError(f"sky capture too narrow ({sky.shape[1]})")
        sky_delta = float(np.abs(top_band(base) - sky[:TOP, :WIDTH, :]).mean())
        if sky_delta < 5.0:
            raise RuntimeError("toolbar bar missing in the sky capture")

        bg_code, bg_log, _ = run("background", "--title",
            {"GX_EDITOR_HOST": "1", "GX_EDITOR_WORKSPACE": "Background"},
            frames=60, shot=40)
        if bg_code != 0:
            raise RuntimeError(f"background host run failed ({bg_code})")
        if "EDITOR_HOST: active=Background" not in bg_log:
            raise RuntimeError("editor registry did not report Background")
        if "EDITOR_HOST: workspace=Background" not in bg_log:
            raise RuntimeError("Background workspace was not applied")
        if "BG_EDITOR_UI: panel=1" not in bg_log:
            raise RuntimeError("background panel did not open")

        #opencode: the GFX editor is hosted now; its panels/viewport must start
        #   below the host menu bar + workspace tab strip.
        base_gfx_code, base_gfx_log, _ = run("gfx-base", "--gfx",
            {"GX_TOOLS_OPEN": "gfx", "GX_DOCK_TRACE": "1"},
            frames=80, shot=60)
        host_gfx_code, host_gfx_log, _ = run("gfx-host", "--gfx",
            {"GX_EDITOR_HOST": "1", "GX_TOOLS_OPEN": "gfx",
             "GX_DOCK_TRACE": "1"}, frames=80, shot=60)
        if base_gfx_code != 0 or host_gfx_code != 0:
            raise RuntimeError("hosted GFX editor run failed")
        if "Tools: GFX editor opened" not in host_gfx_log:
            raise RuntimeError("GFX editor did not open under the host")
        if "EDITOR_HOST: active=GFX" not in host_gfx_log:
            raise RuntimeError("host registry did not report GFX active")
        base_dock = DOCK_RE.findall(base_gfx_log)
        host_dock = DOCK_RE.findall(host_gfx_log)
        if not base_dock or not host_dock:
            raise RuntimeError("GFX dock trace missing")
        base_y = float(base_dock[-1][1])
        host_y = float(host_dock[-1][1])
        if host_y < 40.0:
            raise RuntimeError(
                f"GFX viewport does not clear the host bars (y={host_y:.0f})")
        if host_y <= base_y:
            raise RuntimeError(
                f"host inset did not move the GFX viewport (base {base_y:.0f}, "
                f"host {host_y:.0f})")
        if "panels=1/1" not in host_gfx_log:
            raise RuntimeError("hosted GFX panels do not default to 1/1")
        if "labs=0" not in host_gfx_log:
            raise RuntimeError("GFX lab panels do not default closed")

        focus_code, focus_log, _ = run("gfx-focus", "--gfx",
            {"GX_EDITOR_HOST": "1", "GX_TOOLS_OPEN": "gfx",
             "GX_GFX_PANEL_SCENE": "0", "GX_DOCK_TRACE": "1"},
            frames=80, shot=60)
        noinsp_code, noinsp_log, _ = run("gfx-noinsp", "--gfx",
            {"GX_EDITOR_HOST": "1", "GX_TOOLS_OPEN": "gfx",
             "GX_GFX_PANEL_INSPECTOR": "0", "GX_DOCK_TRACE": "1"},
            frames=80, shot=60)
        if focus_code != 0 or noinsp_code != 0:
            raise RuntimeError("GFX panel toggle runs failed")
        if "panels=0/1" not in focus_log:
            raise RuntimeError("scene panel did not hide")
        if "panels=1/0" not in noinsp_log:
            raise RuntimeError("inspector panel did not hide")
        host_dock = DOCK_RE.findall(host_gfx_log)
        focus_dock = DOCK_RE.findall(focus_log)
        noinsp_dock = DOCK_RE.findall(noinsp_log)
        if not host_dock or not focus_dock or not noinsp_dock:
            raise RuntimeError("GFX panel dock traces missing")
        host_x = float(host_dock[-1][0])
        focus_x = float(focus_dock[-1][0])
        noinsp_x = float(noinsp_dock[-1][0])
        if focus_x >= host_x:
            raise RuntimeError(
                f"hiding the scene panel did not reclaim the left margin "
                f"({focus_x:.0f} vs {host_x:.0f})")
        if noinsp_x <= host_x:
            raise RuntimeError(
                f"hiding the inspector did not widen the viewport start "
                f"({noinsp_x:.0f} vs {host_x:.0f})")

        #opencode: suites carry GFX panel masks; the built-in Focus suite
        #   restores the scene panel hidden.
        suite_code, suite_log, _ = run("gfx-suite", "--gfx",
            {"GX_EDITOR_HOST": "1", "GX_EDITOR_WORKSPACE": "GFX",
             "GX_DOCK_TRACE": "1"}, frames=90, shot=60)
        focus_suite_code, focus_suite_log, _ = run("gfx-focus-suite", "--gfx",
            {"GX_EDITOR_HOST": "1", "GX_EDITOR_WORKSPACE": "GFX Focus",
             "GX_DOCK_TRACE": "1"}, frames=90, shot=60)
        if suite_code != 0 or focus_suite_code != 0:
            raise RuntimeError("GFX suite runs failed")
        if "EDITOR_HOST: workspace=GFX" not in suite_log:
            raise RuntimeError("GFX suite did not apply")
        if "panels=1/1" not in suite_log:
            raise RuntimeError("GFX suite did not restore both panels")
        if "labs=1" not in suite_log:
            raise RuntimeError("GFX suite did not restore the Models lab")
        if "EDITOR_HOST: workspace=GFX Focus" not in focus_suite_log:
            raise RuntimeError("GFX Focus suite did not apply")
        if "panels=0/1" not in focus_suite_log:
            raise RuntimeError("GFX Focus suite did not hide the scene panel")
        if "labs=0" not in focus_suite_log:
            raise RuntimeError("GFX Focus suite did not close the labs")
        suite_dock = DOCK_RE.findall(suite_log)
        focus_suite_dock = DOCK_RE.findall(focus_suite_log)
        if not suite_dock or not focus_suite_dock:
            raise RuntimeError("GFX suite dock traces missing")
        if float(focus_suite_dock[-1][0]) >= float(suite_dock[-1][0]):
            raise RuntimeError("GFX Focus suite did not reclaim the left margin")
        if "aux=0/1" not in host_gfx_log:
            raise RuntimeError("hosted GFX aux panels do not default to 0/1")
        aux_code, aux_log, _ = run("gfx-aux", "--gfx",
            {"GX_EDITOR_HOST": "1", "GX_TOOLS_OPEN": "gfx",
             "GX_GFX_PANEL_ASSET": "1", "GX_GFX_PANEL_FOOTER": "0",
             "GX_DOCK_TRACE": "1"}, frames=90, shot=60)
        if aux_code != 0:
            raise RuntimeError("GFX aux panel run failed")
        if "aux=1/0" not in aux_log:
            raise RuntimeError("GFX asset/footer toggles did not apply")

        #ai: renderer labs are independent floating panels. Summoning two must
        #   draw both and must not resize the docked viewport (they float over
        #   the stage, unlike the fixed scene/inspector panels).
        labs_code, labs_log, _ = run("gfx-labs", "--gfx",
            {"GX_EDITOR_HOST": "1", "GX_TOOLS_OPEN": "gfx",
             "GX_GFX_PANEL_LABS": "3", "GX_DOCK_TRACE": "1"},
            frames=90, shot=60)
        if labs_code != 0:
            raise RuntimeError("GFX lab panel run failed")
        if "labs=3" not in labs_log:
            raise RuntimeError("GFX lab panel mask did not apply")
        for name in ("Models", "Sky"):
            if f"GFX_LAB: panel={name} open=1" not in labs_log:
                raise RuntimeError(f"GFX {name} lab panel did not draw")
        labs_dock = DOCK_RE.findall(labs_log)
        if not labs_dock:
            raise RuntimeError("GFX lab dock trace missing")
        if labs_dock[-1][:4] != host_dock[-1][:4]:
            raise RuntimeError("GFX lab panels resized the docked viewport")

        #ai: Basic/Advanced/Expert surface coverage — labs and the gated
        #   sections must match the declared minimum level at runtime.
        uiRows = {}
        for level in ("Basic", "Advanced", "Expert"):
            uiRows[level] = check_ui_level(
                f"ui-{level.lower()}", level, {"GX_TOOLS_OPEN": "gfx"})

        #ai: virtual-screen/present-filter contract — 320x240 render into a
        #   1024x768 window, with a measurable Soft/Sharp blockiness split.
        consoleSoft = check_console_240p("console-soft", "soft")
        consoleSharp = check_console_240p("console-sharp", "sharp")
        if consoleSharp <= 0.5 or consoleSoft >= consoleSharp * 0.6:
            raise RuntimeError(
                f"console 240p filter split weak "
                f"(soft {consoleSoft:.3f}, sharp {consoleSharp:.3f})")

        #ai: Console Max closure at 320 must beat the same preset at 1024.
        consoleMax320 = check_console_max("console-max-320", True)
        consoleMax1024 = check_console_max("console-max-1024", False)
        if consoleMax320 <= 0.0 or consoleMax1024 <= 0.0 \
                or consoleMax320 >= consoleMax1024 * 0.9:
            raise RuntimeError(
                f"console max budget weak "
                f"(320 {consoleMax320:.2f} ms, 1024 {consoleMax1024:.2f} ms)")
        print(f"CONSOLE_MAX: 320x240 {consoleMax320:.2f} ms vs "
              f"1024x768 {consoleMax1024:.2f} ms")

        #ai: the preset browser root is env-overridable for authoring outside
        #   the shipped resource tree.
        with tempfile.TemporaryDirectory(prefix="gx_presets_",
                                         dir="/tmp/opencode") as presetRoot, \
             tempfile.TemporaryDirectory(prefix="gx_guest_",
                                         dir="/tmp/opencode") as guestRoot:
            presetDir = Path(presetRoot)
            Path(guestRoot, "tools").mkdir(parents=True, exist_ok=True)
            source = audit.ROOT / "resources/tools/gfx_showcase.json"
            shutil.copyfile(source, presetDir / "custom.json")
            shutil.copyfile(source, Path(guestRoot) / "tools" / "guest.json")
            preset_code, preset_log, _ = run("gfx-preset-dir", "--gfx",
                {"GX_EDITOR_HOST": "1", "GX_TOOLS_OPEN": "gfx",
                 "GX_GFX_PRESET_DIR": presetRoot,
                 "GX_ASSETS": guestRoot,
                 "GX_GFX_PRESET_SCAN": "1", "GX_DOCK_TRACE": "1"},
                frames=60, shot=40)
            if preset_code != 0:
                raise RuntimeError("GFX preset-dir run failed")
            if f"GFX_PRESET_SCAN: primary={presetRoot} roots=4 presets=2" not in preset_log:
                raise RuntimeError("GFX preset-dir/guest scan mismatch")
            if f"GFX_PRESET_DEFAULT: path={presetRoot}/untitled.json" not in preset_log:
                raise RuntimeError("GFX preset-dir default save path is wrong")

        default_code, default_log, _ = run("default", "--title",
            {"GX_EDITOR_HOST": "1", "GX_EDITOR_WORKSPACE": "Default"},
            frames=40, shot=30)
        if default_code != 0:
            raise RuntimeError(f"default workspace run failed ({default_code})")
        if "EDITOR_HOST: workspace=Default" not in default_log:
            raise RuntimeError("Default workspace was not applied")
        if "EDITOR_HOST: active=none" not in default_log:
            raise RuntimeError("Default workspace unexpectedly opened an editor")

        trace_code, trace_log, _ = run("tabs", "--title",
            {"GX_EDITOR_HOST": "1", "GX_EDITOR_WORKSPACE": "Default",
             "GX_EDITOR_HOST_TAB_TRACE": "1"}, frames=60, shot=40)
        match = TAB_RE.search(trace_log)
        if trace_code != 0 or not match:
            raise RuntimeError("workspace tab rect trace missing")
        #opencode: every tab owns only its own hit box. A full-row Selectable
        #   (the v0.30.1 host bug) made the first tab swallow every click.
        tabs = [(name, int(x), int(y), int(w), int(h))
                for name, x, y, w, h in TAB_ALL_RE.findall(trace_log)]
        if len(tabs) < 3:
            raise RuntimeError(f"workspace tab rects missing ({len(tabs)})")
        for name, x, y, w, h in tabs:
            if w > 200:
                raise RuntimeError(
                    f"workspace tab '{name}' is too wide ({w}px)")
        for index in range(len(tabs) - 1):
            name, x, y, w, h = tabs[index]
            next_x = tabs[index + 1][1]
            if x + w > next_x:
                raise RuntimeError(
                    f"workspace tab '{name}' overlaps the next tab")
    except (OSError, RuntimeError, ValueError) as error:
        print(f"EDITOR_HOST_CHECK: FAIL — {error}", file=sys.stderr)
        return 1
    print("EDITOR_HOST_CHECK: PASS toolbar in title/gfx, F9 open+close, "
          "Sky/Background/Default workspaces, tab hit boxes sane, GFX hosted "
          "below the bars, scene/inspector panels summonable, "
          "GFX/GFX Focus suite masks, aux asset/footer panels, "
          "renderer lab panels summonable without moving the dock, "
          "Basic/Advanced/Expert lab and section coverage, "
          "320x240 virtual screen Soft/Sharp split, "
          "Console Max 320<1024 frame budget, "
          "preset-dir override")
    return 0


if __name__ == "__main__":
    sys.exit(main())

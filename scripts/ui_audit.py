#!/usr/bin/env python3
#opencode: UI audit — tool wiring matrix + panel conventions (+ optional
#   captures for the vision review). The mechanical part is a gate: every
#   editor must be reachable through all three open paths (CLI flag,
#   GX_TOOLS_OPEN value, EXTRAS card) with its runtime flag/handler in place,
#   and each editor panel must follow the shared conventions (title prefix,
#   first-use size, status line). Captures land in a directory for the
#   layout/discoverability review recorded in docs/ui_audit.md.
import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

import audit_common as audit


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
BIN = ROOT / "build" / "gX_build"
MODULE_BIN = ROOT / "build" / "scripting" / "gX_build"
MODULE_SO = ROOT / "build" / "scripting" / "gx_shell.so"
DISPLAY = os.environ.get("DISPLAY", ":1")

#opencode: tool table — key, opener suffix, kind, env value, CLI flag, EXTRAS
#   label (None = deliberately card-less), panel source, module-hosted.
#   kinds:
#     window  — single panel: opener+flag+cli+env+card+handler, conventions
#     canvas  — GUI-template gallery: opener+cli+card+handler (+env when it
#               has one), no runtime flag/panel of its own
#     dock    — multi-window suite with its own coherent prefix (advisory)
#     module  — panel drawn by gx_shell (no EXTRAS card for the debugger)
TOOLS = [
    ("editor",     "Editor",     "dock",   "editor",     "--tools",            "GUI EDITOR",         "src/tools/editor/editor.cpp",                False),
    ("theme",      "Theme",      "window", "theme",      "--theme-editor",     "THEME EDITOR",       "src/tools/theme/theme_editor.cpp",           False),
    ("themlab",    "ThemLab",    "canvas", "themlab",    "--themlab",          "THEME LAB",          "src/gui/templates/themlab.c",                False),
    ("materials",  "Materials",  "canvas", "materials",  "--materialslab",     "MATERIAL LAB",       "src/gui/templates/materialslab.c",           False),
    ("shaderlab",  "ShaderLab",  "canvas", None,         "--shaderlab",        "SHADER LAB",         "src/gui/templates/shaderlab.c",              False),
    ("gfx",        "Gfx",        "dock",   "gfx",        "--gfx-editor",       "GFX EDITOR",         "src/tools/gfx/gfx_editor.cpp",               False),
    ("script",     "Script",     "module", "script",     "--script-editor",    None,                 "src/tools/script/script_panel.cpp",          True),
    ("wavetable",  "Wavetable",  "window", "wavetable",  "--wavetable-editor", "WAVETABLE EDITOR",   "src/tools/synth/wavetable_editor.cpp",       False),
    ("textwriter", "Textwriter", "window", "textwriter", "--textwriter-editor","TEXTWRITER EDITOR", "src/tools/textwriter/textwriter_editor.cpp", False),
    ("physics",    "Physics",    "window", "physics",    "--physics-editor",   "PHYSICS EDITOR",     "src/tools/physics/physics_editor.cpp",       False),
    ("rail",       "Rail",       "module", "rail",       "--rail-editor",      "RAIL EDITOR",        "src/tools/tour/rail_editor.cpp",             True),
    ("background", "Background", "window", "background", "--background-editor","BACKGROUND EDITOR",  "src/tools/background/background_editor.cpp", False),
    ("sky",        "Sky",        "window", "sky",        "--sky-editor",       "SKY EDITOR",         "src/tools/sky/sky_editor.cpp",               False),
]


def read(rel):
    path = ROOT / rel
    return path.read_text(errors="ignore") if path.is_file() else ""


def main():
    parser = argparse.ArgumentParser(description="gX UI audit")
    parser.add_argument("--capture", metavar="DIR",
                        help="launch every tool and save screenshots for review")
    arguments = parser.parse_args()

    tools_c = read("src/tools/tools.c")
    parse_c = read("src/main_parse.c")
    load_c = read("src/main_load.c")
    titlegx_c = read("src/gui/templates/titlegx.c")
    binding_c = read("src/gui/binding.c")
    binding_h = read("src/gui/binding.h")
    editors_md = read("docs/editors.md")
    api_md = read("docs/api_reference.md")

    failures = []
    warnings = []

    print("== [gX UI audit] tool wiring matrix ==")
    for key, suffix, kind, env, cli, card, panel, module in TOOLS:
        opener = f"gX_Tools_Open{suffix}_Runtime"
        flag = f"gX_Tools_Runtime{suffix}Flag"
        handler = f"gX_Widget_OnClick_Open{suffix}"

        missing = []
        if opener not in tools_c:
            missing.append("opener")
        if kind != "canvas" and flag not in tools_c:
            missing.append("flag-getter")
        if f'{{"{cli[2:]}"' not in parse_c:
            missing.append("cli-flag")
        if env and f'strcmp(openEnv, "{env}")' not in load_c:
            missing.append("env-value")
        if card:
            if f'"{card}"' not in titlegx_c:
                missing.append("extras-card")
            if handler not in titlegx_c:
                missing.append("card-handler")
            if handler not in binding_c or handler not in binding_h:
                missing.append("handler-def")
        else:
            warnings.append(f"{key}: no EXTRAS card (CLI/env only — document the reason)")
        display = {"themlab": "ThemLab", "shaderlab": "ShaderLab",
                   "gfx": "GFX editor", "editor": "GUI editor",
                   "materials": "MaterialsLab", "script": "Script debugger",
                   "rail": "Camera rail editor", "sky": "Sky editor",
                   "background": "Background editor",
                   "wavetable": "Wavetable editor",
                   "textwriter": "Textwriter editor",
                   "physics": "Physics editor", "theme": "Theme editor"}[key]
        if display not in editors_md:
            warnings.append(f"{key}: not in docs/editors.md inventory")
        if suffix not in api_md:
            missing.append("api-reference")

        status = "ok" if not missing else "MISSING " + ",".join(missing)
        print(f"  {key:11s} {kind:6s} {'(module) ' if module else '         '}{status}")
        if missing:
            failures.append(f"{key}: {','.join(missing)}")

    print("== [gX UI audit] panel conventions ==")
    for key, _suffix, kind, _env, _cli, _card, panel, _module in TOOLS:
        if kind == "canvas":
            continue  # GUI-template galleries, not ImGui windows
        source = read(panel)
        if not source:
            failures.append(f"{key}: panel source missing ({panel})")
            continue
        problems = []
        if kind == "window" and 'ImGui::Begin("gX Tools - ' not in source:
            problems.append("title-prefix")
        if kind in ("window", "module") and "SetNextWindowSize" not in source:
            problems.append("first-use-size")
        if (kind in ("window", "module")
                and "TextUnformatted(" not in source and "ImGui::Text(" not in source):
            problems.append("status-line")
        status = "ok" if not problems else "CHECK " + ",".join(problems)
        print(f"  {key:11s} {kind:6s} {status}")
        if problems:
            warnings.append(f"{key}: panel conventions {','.join(problems)}")

    if arguments.capture:
        out = Path(arguments.capture)
        out.mkdir(parents=True, exist_ok=True)
        print(f"== [gX UI audit] captures -> {out} ==")
        for key, _suffix, _kind, env, cli, _card, _panel, module in TOOLS:
            binary = MODULE_BIN if module else BIN
            if module and (not MODULE_SO.is_file() or not Path(MODULE_BIN).is_file()):
                print(f"  {key:11s} skipped (no module build)")
                warnings.append(f"capture {key}: skipped (no module build)")
                continue
            for width, height in ((1024, 768), (1600, 900)):
                envs = dict(os.environ)
                envs["DISPLAY"] = DISPLAY
                envs["GX_NO_SETTINGS_SAVE"] = "1"
                envs["GX_SYNTH_MUTE"] = "1"
                #ai: the sandbox plays procedural impact/tool SFX now — audit
                #   captures stay silent (the synth mute precedent).
                envs["GX_AUDIO_MUTE"] = "1"
                envs["GX_SCREENSHOT_DIR"] = str(out)
                envs["GX_FRAMES"] = "60"
                envs.pop("GX_SCRIPT_SCAN", None)
                envs.pop("GX_TOOLS_OPEN", None)
                args = [str(binary), "--screenWidth", str(width),
                        "--screenHeight", str(height),
                        "--ai-screenshot-after", "40"]
                if env:
                    envs["GX_TOOLS_OPEN"] = env
                else:
                    args.append(cli)
                if module:
                    envs["GX_MODULE"] = str(MODULE_SO)
                result = subprocess.run(args, cwd=str(ROOT), env=envs,
                                        capture_output=True, text=True, timeout=180)
                shot = audit.newest_shot(out)
                target = None
                if result.returncode == 0 and shot:
                    target = out / f"{key}_{width}x{height}.png"
                    shot.rename(target)
                print(f"  {key:11s} {width}x{height} rc={result.returncode} shot={target}")
                if result.returncode != 0:
                    warnings.append(f"capture {key} {width}x{height}: exit {result.returncode}")

    if warnings:
        print("== [gX UI audit] notes ==")
        for warning in warnings:
            print(f"  note: {warning}")
    if failures:
        print(f"UI_AUDIT: FAIL ({len(failures)} wiring gap(s))")
        return 1
    print("UI_AUDIT: PASS (wiring complete; see notes for advisories)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

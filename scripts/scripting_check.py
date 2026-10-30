#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: scripting shell gate — behavior probes over the module build
#   (build/scripting: exe + gx_shell.so). Verifies the module contract (export
#   list drift, explicit/GX_MODULE load, handshake, missing-module failures)
#   plus the shell itself: load/compile/execute, cross-frame Sleep/Yield,
#   shell asserts + exit codes, the state KV, Vec3, named hooks with
#   scene_enter/scene_exit wiring, mtime hot-reload, console, tour APIs and
#   the markup companion-script lazy-load.
#
#   The default build never loads modules by itself; probes pass GX_MODULE (the
#   Makefile target builds build/scripting first — see check-scripting).

import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

import audit_common as audit


ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT / "build" / "scripting" / "gX_build"
MODULE = ROOT / "build" / "scripting" / "gx_shell.so"
EXE_OBJDIR = ROOT / "build" / "scripting" / "CMakeFiles" / "gX_build.dir"
SCRATCH = audit.SCRATCH
DISPLAY = os.environ.get("DISPLAY", ":1")
TIMEOUT = 120

PASS_RE = re.compile(r"\[SHELL\] PASS:")
FAIL_RE = re.compile(r"\[SHELL\] FAIL:")


def clean_env(**extra):
    environment = {k: v for k, v in os.environ.items() if not k.startswith("GX_")}
    environment["DISPLAY"] = DISPLAY
    environment["GX_NO_SETTINGS_SAVE"] = "1"
    #ai: synth output stays muted in gates — the engine path still runs
    #   (voices/scope/mixer), but no sound is emitted; probes assert the
    #   mute state and the off-by-default contract explicitly.
    environment["GX_SYNTH_MUTE"] = "1"
    #ai: the OPX/PAD engines are muted the same way in gates (their probes
    #   assert the mute state and the off-by-default contract explicitly).
    environment["GX_OPX_MUTE"] = "1"
    environment["GX_PAD_MUTE"] = "1"
    #probes drive their own scripts; the resources/script boot scan stays off
    #   unless a probe explicitly enables it.
    environment["GX_SCRIPT_SCAN"] = "0"
    #opencode: dynamic module — the shell lives in gx_shell.so now; probes load
    #   it through GX_MODULE unless they explicitly override ("" disables).
    environment["GX_MODULE"] = str(MODULE)
    environment.update(extra)
    return environment


def run_script(script_path, frames=200, timeout=TIMEOUT):
    result = subprocess.run(
        [str(BIN), "--script", str(script_path)],
        cwd=str(ROOT), env=clean_env(GX_FRAMES=str(frames)),
        capture_output=True, text=True, timeout=timeout)
    return result.returncode, audit.strip_ansi(result.stdout + result.stderr)


def check(name, condition, detail, failures):
    if condition:
        print(f"  PASS {name}")
        return
    print(f"  FAIL {name}: {detail}")
    failures.append(name)


def probe_module(workdir, failures):
    #opencode: Route A ABI — the checked-in export list must match a fresh
    #   generation from the module's undefined symbols (regenerate with
    #   `make module-exports`; the list is append-only within the ABI major).
    list_check = subprocess.run(
        [sys.executable, str(ROOT / "scripts" / "module_exports.py"), "--check",
         str(EXE_OBJDIR), str(MODULE)],
        cwd=str(ROOT), capture_output=True, text=True, timeout=TIMEOUT)
    check("module: export list current (Route A ABI)",
          list_check.returncode == 0 and "OK" in list_check.stdout,
          (list_check.stdout + list_check.stderr).strip()[:140], failures)

    script = workdir / "module_probe.as"
    script.write_text(
        "void main()\n"
        "{\n"
        "    Print(\"module probe ran\");\n"
        "    Quit(0);\n"
        "}\n")

    #explicit --module load: entry handshake + scripting entry both run
    result = subprocess.run(
        [str(BIN), "--module", str(MODULE), "--script", str(script)],
        cwd=str(ROOT), env=clean_env(GX_FRAMES="90"),
        capture_output=True, text=True, timeout=TIMEOUT)
    log = audit.strip_ansi(result.stdout + result.stderr)
    check("module: explicit load + handshake",
          result.returncode == 0 and "GX_SHELL: entry" in log
          and "MODULE: loaded" in log and "module probe ran" in log,
          f"exit {result.returncode}", failures)

    #a missing explicit module fails the boot loudly (dlopen error logged)
    result = subprocess.run(
        [str(BIN), "--module", str(workdir / "missing_module.so")],
        cwd=str(ROOT), env=clean_env(GX_FRAMES="30"),
        capture_output=True, text=True, timeout=TIMEOUT)
    log = audit.strip_ansi(result.stdout + result.stderr)
    check("module: missing path fails the boot loudly",
          result.returncode != 0 and "dlopen" in log,
          f"exit {result.returncode}", failures)

    #--script without a module: warned and ignored, clean exit
    result = subprocess.run(
        [str(BIN), "--script", str(script)],
        cwd=str(ROOT), env=clean_env(GX_MODULE="", GX_FRAMES="30"),
        capture_output=True, text=True, timeout=TIMEOUT)
    log = audit.strip_ansi(result.stdout + result.stderr)
    check("module: --script without a module warns and is ignored",
          result.returncode == 0 and "needs a module" in log,
          f"exit {result.returncode}", failures)


def probe_pack(workdir, failures):
    script = workdir / "shell_pack.as"
    script.write_text(
        "void main()\n"
        "{\n"
        "    Print(\"shell lives\");\n"
        "    AssertTrue(1 + 1 == 2, \"math works\");\n"
        "    AssertEqual(3, 3, \"int equality\");\n"
        "    AssertFloat(0.1f + 0.2f, 0.3f, 0.0001f, \"float close\");\n"
        "    Vec3 v = Vec3_Create(1.0f, 2.0f, 3.0f);\n"
        "    AssertFloat(v.Length(), Sqrt(14.0f), 0.001f, \"vec length\");\n"
        "    ScriptSetInt(\"smoke\", 42);\n"
        "    AssertEqual(ScriptGetInt(\"smoke\", 0), 42, \"state kv\");\n"
        "    Hook(\"smoke_hook\", OnSmoke);\n"
        "    TriggerHook(\"smoke_hook\");\n"
        "    Quit(0);\n"
        "}\n"
        "void OnSmoke()\n"
        "{\n"
        "    AssertTrue(true, \"hook fired\");\n"
        "}\n")
    code, log = run_script(script)
    check("pack: exit 0", code == 0, f"exit {code}", failures)
    check("pack: all asserts pass (6)",
          len(PASS_RE.findall(log)) == 6 and not FAIL_RE.search(log),
          f"{len(PASS_RE.findall(log))} pass lines, fail={bool(FAIL_RE.search(log))}",
          failures)
    check("pack: hook fired", "hook fired" in log, "hook line missing", failures)


def probe_fail(workdir, failures):
    script = workdir / "shell_fail.as"
    script.write_text(
        "void main()\n"
        "{\n"
        "    AssertTrue(false, \"intentional failure\");\n"
        "    AssertEqual(1, 2, \"int mismatch\");\n"
        "}\n")
    code, log = run_script(script)
    check("fail: exit non-zero", code != 0, f"exit {code}", failures)
    check("fail: FAIL lines recorded", len(FAIL_RE.findall(log)) == 2,
          f"{len(FAIL_RE.findall(log))} fail lines", failures)


def probe_sleep(workdir, failures):
    script = workdir / "shell_sleep.as"
    script.write_text(
        "int tick = 0;\n"
        "void main()\n"
        "{\n"
        "    while (tick < 3)\n"
        "    {\n"
        "        Print(\"tick\");\n"
        "        tick = tick + 1;\n"
        "        Sleep(120.0f);\n"
        "    }\n"
        "    Print(\"tocks done\");\n"
        "    Quit(0);\n"
        "}\n")
    code, log = run_script(script)
    ticks = log.count("[Script] tick")
    check("sleep: exit 0", code == 0, f"exit {code}", failures)
    check("sleep: 3 cross-frame ticks then completion",
          ticks == 3 and "tocks done" in log,
          f"{ticks} ticks, done={'tocks done' in log}", failures)


def probe_scene_hooks(workdir, failures):
    script = workdir / "scene_hooks.as"
    script.write_text(
        "void main()\n"
        "{\n"
        "    Hook(\"scene_enter\", OnEnter);\n"
        "    Hook(\"scene_exit\", OnExit);\n"
        "    SwitchScene(SCENE_TITLE);\n"
        "    Sleep(300.0f);\n"
        "    Quit(0);\n"
        "}\n"
        "void OnEnter() { Print(\"scene enter fired\"); }\n"
        "void OnExit()  { Print(\"scene exit fired\"); }\n")
    code, log = run_script(script)
    enter = log.find("scene enter fired")
    exit_at = log.find("scene exit fired")
    check("scene: exit 0", code == 0, f"exit {code}", failures)
    check("scene: exit fires before enter", 0 <= exit_at < enter,
          f"enter@{enter} exit@{exit_at}", failures)


def probe_hot_reload(workdir, failures):
    script = workdir / "hotreload.as"
    script.write_text(
        "void main()\n"
        "{\n"
        "    while (true)\n"
        "    {\n"
        "        Print(\"alive v1\");\n"
        "        Sleep(250.0f);\n"
        "    }\n"
        "}\n")
    log_path = workdir / "hotreload.log"
    with open(log_path, "w") as stream:
        process = subprocess.Popen(
            [str(BIN), "--script", str(script)],
            cwd=str(ROOT), env=clean_env(GX_FRAMES="600"),
            stdout=stream, stderr=subprocess.STDOUT)
        try:
            time.sleep(2.5)
            script.write_text(script.read_text().replace("alive v1", "alive v2"))
            os.utime(script)
            time.sleep(4.0)
        finally:
            try:
                process.wait(timeout=30)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=10)
    log = audit.strip_ansi(log_path.read_text())
    reload_at = log.find("hot-reload")
    v2_at = log.find("alive v2", reload_at if reload_at >= 0 else 0)
    check("reload: change picked up", reload_at >= 0, "no hot-reload line", failures)
    check("reload: script resumes after rebuild", v2_at > reload_at and reload_at >= 0,
          "no 'alive v2' after the reload", failures)


def probe_scan(workdir, failures):
    scanDir = workdir / "scan_src"
    scanDir.mkdir()
    (scanDir / "a.as").write_text('void main() { Print("scan a ran"); }\n')
    (scanDir / "b.as").write_text('void main() { Print("scan b ran"); }\n')
    (scanDir / "ignored.txt").write_text("not a script\n")

    result = subprocess.run(
        [str(BIN)], cwd=str(ROOT),
        env=clean_env(GX_FRAMES="30", GX_SCRIPT_DIR=str(scanDir),
                      GX_SCRIPT_SCAN="1"),
        capture_output=True, text=True, timeout=TIMEOUT)
    log = audit.strip_ansi(result.stdout + result.stderr)
    check("scan: boot dir loads both scripts",
          "scan a ran" in log and "scan b ran" in log and "ignored.txt" not in log,
          "missing boot-scan output", failures)

    result = subprocess.run(
        [str(BIN)], cwd=str(ROOT),
        env=clean_env(GX_FRAMES="30", GX_SCRIPT_DIR=str(scanDir)),
        capture_output=True, text=True, timeout=TIMEOUT)
    log = audit.strip_ansi(result.stdout + result.stderr)
    check("scan: GX_SCRIPT_SCAN=0 opts out",
          "scan a ran" not in log and "scan b ran" not in log,
          "scripts ran despite opt-out", failures)


def probe_mod_scan(workdir, failures):
    modRoot = workdir / "mod_root"
    modScripts = modRoot / "mymod" / "script"
    modScripts.mkdir(parents=True)
    (modScripts / "mod.as").write_text('void main() { Print("mod ran"); }\n')

    result = subprocess.run(
        [str(BIN)], cwd=str(ROOT),
        env=clean_env(GX_FRAMES="30", GX_SCRIPT_MOD_DIR=str(modRoot)),
        capture_output=True, text=True, timeout=TIMEOUT)
    log = audit.strip_ansi(result.stdout + result.stderr)
    check("mod: custom-root scan loads mod script", "mod ran" in log,
          "mod script did not run", failures)


def probe_dom_click(workdir, failures):
    handler = workdir / "dom_handler.as"
    handler.write_text(
        "void OnClick()\n"
        "{\n"
        "    ScriptSetInt(\"onclick_fired\", 1);\n"
        "}\n")
    script = workdir / "dom_click.as"
    script.write_text(
        "void main()\n"
        "{\n"
        "    int elem = -1;\n"
        "    int tries = 0;\n"
        "    while (elem < 0 && tries < 200)\n"
        "    {\n"
        "        elem = GUIFindElement(2, \"hello\");\n"
        "        tries = tries + 1;\n"
        "        Sleep(16.0f);\n"
        "    }\n"
        "    AssertTrue(elem >= 0, \"markup element found\");\n"
        "    AssertTrue(GUIGetElementText(2, elem) == \"SCRIPT SMOKE\", \"element text\");\n"
        "    GUIClickElement(2, elem);\n"
        "    Sleep(300.0f);\n"
        "    AssertEqual(ScriptGetInt(\"onclick_fired\", 0), 1, \"onclick handler ran\");\n"
        "    Quit(0);\n"
        "}\n")
    markup = workdir / "dom_markup.json"
    markup.write_text(
        "{\n"
        "  \"name\": \"dom_probe\",\n"
        "  \"description\": \"scripting DOM click probe\",\n"
        "  \"css\": \"\",\n"
        f"  \"script\": \"{handler}\",\n"
        "  \"templateID\": 0,\n"
        "  \"elements\": [\n"
        "    {\n"
        "      \"type\": \"button\",\n"
        "      \"name\": \"hello\",\n"
        "      \"class\": \"\", \"style\": \"\",\n"
        "      \"gx\": 2.8, \"gy\": 3.4, \"gw\": 2.4, \"gh\": 0.5,\n"
        "      \"color\": [50, 40, 80, 180],\n"
        "      \"text\": \"SCRIPT SMOKE\",\n"
        "      \"fontSize\": 0, \"texture\": \"\", \"shader\": \"\",\n"
        "      \"display\": \"block\", \"layer\": 0, \"visible\": true,\n"
        "      \"centerX\": false, \"centerY\": false,\n"
        "      \"onclick\": \"OnClick\",\n"
        "      \"children\": []\n"
        "    }\n"
        "  ]\n"
        "}\n")
    result = subprocess.run(
        [str(BIN), "--script", str(script)],
        cwd=str(ROOT),
        env=clean_env(GX_FRAMES="300", GX_GUI_MARKUP=str(markup)),
        capture_output=True, text=True, timeout=TIMEOUT)
    log = audit.strip_ansi(result.stdout + result.stderr)
    check("dom: click reaches markup onclick", result.returncode == 0
          and "onclick handler ran (1 == 1)" in log,
          f"exit {result.returncode}", failures)


def probe_debugger(workdir, failures):
    scanDir = workdir / "dbg_src"
    scanDir.mkdir()
    (scanDir / "worker.as").write_text(
        "void main()\n"
        "{\n"
        "    while (true)\n"
        "    {\n"
        "        ScriptSetInt(\"worker_step\", ScriptGetInt(\"worker_step\", 0) + 1);\n"
        "        Sleep(50.0f);\n"
        "    }\n"
        "}\n")
    (scanDir / "driver.as").write_text(
        "void main()\n"
        "{\n"
        "    int worker = -1;\n"
        "    int i;\n"
        "    for (i = 0; i < ScriptCount(); i++)\n"
        "    {\n"
        "        string p = ScriptGetPath(i);\n"
        "        if (p.findFirst(\"worker.as\") >= 0) worker = i;\n"
        "    }\n"
        "    AssertTrue(worker >= 0, \"worker found\");\n"
        "    DebugToggleBreakpoint(worker, 5);\n"
        "    DebugToggleBreakpoint(worker, 6);\n"
        "    Sleep(300.0f);\n"
        "    AssertTrue(DebugIsPaused(worker), \"worker paused at breakpoint\");\n"
        "    int line = DebugGetLine(worker);\n"
        "    AssertTrue(line == 5 || line == 6, \"paused on a breakpoint line\");\n"
        "    DebugClearBreakpoints(worker);\n"
        "    DebugContinue(worker);\n"
        "    int before = ScriptGetInt(\"worker_step\", 0);\n"
        "    Sleep(300.0f);\n"
        "    AssertTrue(ScriptGetInt(\"worker_step\", 0) > before, \"worker advanced after continue\");\n"
        "    Quit(0);\n"
        "}\n")
    result = subprocess.run(
        [str(BIN)], cwd=str(ROOT),
        env=clean_env(GX_FRAMES="300", GX_SCRIPT_DIR=str(scanDir),
                      GX_SCRIPT_SCAN="1"),
        capture_output=True, text=True, timeout=TIMEOUT)
    log = audit.strip_ansi(result.stdout + result.stderr)
    check("debugger: breakpoint pauses the worker", result.returncode == 0
          and "worker paused at breakpoint" in log and "paused on a breakpoint line" in log,
          f"exit {result.returncode}", failures)
    check("debugger: continue resumes the worker",
          "worker advanced after continue" in log, "worker did not advance",
          failures)


def probe_state_persistence(workdir, failures):
    statePath = workdir / "state_probe.json"
    saveScript = workdir / "state_save.as"
    saveScript.write_text(
        "void main()\n"
        "{\n"
        "    ScriptSetInt(\"persist_me\", 7);\n"
        "    ScriptSetFloat(\"persist_f\", 2.5f);\n"
        "    ScriptSetString(\"persist_str\", \"hello state\");\n"
        "    Quit(0);\n"
        "}\n")
    checkScript = workdir / "state_check.as"
    checkScript.write_text(
        "void main()\n"
        "{\n"
        "    AssertEqual(ScriptGetInt(\"persist_me\", 0), 7, \"int persisted\");\n"
        "    AssertFloat(ScriptGetFloat(\"persist_f\", 0.0f), 2.5f, 0.001f, \"float persisted\");\n"
        "    AssertTrue(ScriptGetString(\"persist_str\") == \"hello state\", \"string persisted\");\n"
        "    Quit(0);\n"
        "}\n")
    environment = clean_env(GX_FRAMES="60", GX_SCRIPT_STATE=str(statePath))
    first = subprocess.run([str(BIN), "--script", str(saveScript)], cwd=str(ROOT),
                           env=environment, capture_output=True, text=True,
                           timeout=TIMEOUT)
    check("state: save run writes the file", first.returncode == 0 and statePath.is_file(),
          f"exit {first.returncode}, file={statePath.is_file()}", failures)
    second = subprocess.run([str(BIN), "--script", str(checkScript)], cwd=str(ROOT),
                            env=environment, capture_output=True, text=True,
                            timeout=TIMEOUT)
    log = audit.strip_ansi(second.stdout + second.stderr)
    check("state: fresh process reloads all three types",
          second.returncode == 0 and "int persisted" in log
          and "float persisted" in log and "string persisted" in log,
          f"exit {second.returncode}", failures)


def probe_script_panel(workdir, failures):
    result = subprocess.run(
        [str(BIN), "--script-editor"], cwd=str(ROOT),
        env=clean_env(GX_FRAMES="30"),
        capture_output=True, text=True, timeout=TIMEOUT)
    log = audit.strip_ansi(result.stdout + result.stderr)
    check("panel: --script-editor opens and exits clean",
          result.returncode == 0 and "scripting debugger opened" in log,
          f"exit {result.returncode}", failures)


def probe_console_exec(workdir, failures):
    batch = "\n".join([
        "/echo console-alive",
        "/stats",
        "/scripts",
        "/state",
        "/ls resources/script",
    ])
    result = subprocess.run(
        [str(BIN)], cwd=str(ROOT),
        env=clean_env(GX_FRAMES="30", GX_CONSOLE_EXEC=batch),
        capture_output=True, text=True, timeout=TIMEOUT)
    log = audit.strip_ansi(result.stdout + result.stderr)
    markers = ["console-alive", "=== Engine stats ===", "=== Scripts (",
               "=== Script state:", "=== Directory: resources/script"]
    missing = [m for m in markers if m not in log]
    check("console: GX_CONSOLE_EXEC runs built-ins",
          result.returncode == 0 and not missing,
          f"exit {result.returncode}, missing {missing}", failures)


def probe_console_script(workdir, failures):
    statePath = workdir / "console_state.json"
    environment = clean_env(GX_FRAMES="30", GX_SCRIPT_STATE=str(statePath),
                            GX_CONSOLE_EXEC='ScriptSetInt("console_ran", 1)')
    first = subprocess.run([str(BIN)], cwd=str(ROOT), env=environment,
                           capture_output=True, text=True, timeout=TIMEOUT)
    checkScript = workdir / "console_check.as"
    checkScript.write_text(
        "void main()\n"
        "{\n"
        "    AssertEqual(ScriptGetInt(\"console_ran\", 0), 1, \"console one-liner set state\");\n"
        "    Quit(0);\n"
        "}\n")
    second = subprocess.run(
        [str(BIN), "--script", str(checkScript)], cwd=str(ROOT),
        env=clean_env(GX_FRAMES="60", GX_SCRIPT_STATE=str(statePath)),
        capture_output=True, text=True, timeout=TIMEOUT)
    log = audit.strip_ansi(second.stdout + second.stderr)
    check("console: one-liner runs through the script engine",
          first.returncode == 0 and second.returncode == 0
          and "console one-liner set state (1 == 1)" in log,
          f"exit {first.returncode}/{second.returncode}", failures)

    bad = subprocess.run(
        [str(BIN)], cwd=str(ROOT),
        env=clean_env(GX_FRAMES="30", GX_CONSOLE_EXEC="this is not valid;)"),
        capture_output=True, text=True, timeout=TIMEOUT)
    badLog = audit.strip_ansi(bad.stdout + bad.stderr)
    check("console: script errors are reported, not fatal",
          bad.returncode == 0 and "Script error" in badLog,
          f"exit {bad.returncode}", failures)


def probe_console_panel(workdir, failures):
    result = subprocess.run(
        [str(BIN), "--console"], cwd=str(ROOT),
        env=clean_env(GX_FRAMES="30"),
        capture_output=True, text=True, timeout=TIMEOUT)
    log = audit.strip_ansi(result.stdout + result.stderr)
    check("console: --console opens over a scene and exits clean",
          result.returncode == 0 and "ImGui loaded" in log,
          f"exit {result.returncode}", failures)


def probe_tour_apis(workdir, failures):
    script = workdir / "tour_probe.as"
    presetPath = workdir / "bg_probe.json"
    script.write_text(
        "void main()\n"
        "{\n"
        "    Camera_SetPos(Vec3_Create(1.0f, 2.0f, 3.0f));\n"
        "    Camera_SetTarget(Vec3_Create(0.0f, 1.0f, 0.0f));\n"
        "    Camera_SetFov(60.0f);\n"
        "    Vec3 p = Camera_GetPos();\n"
        "    AssertFloat(p.x, 1.0f, 0.001f, \"camera pos\");\n"
        "    AssertFloat(Camera_GetFov(), 60.0f, 0.001f, \"camera fov\");\n"
        "    Camera_Keyframe_Clear();\n"
        "    Camera_Keyframe_Add(0.0f, Vec3_Create(5.0f, 2.0f, 5.0f), Vec3_Create(0.0f, 0.0f, 0.0f), 45.0f);\n"
        "    Camera_Keyframe_Add(0.4f, Vec3_Create(-5.0f, 2.0f, 5.0f), Vec3_Create(0.0f, 0.0f, 0.0f), 45.0f);\n"
        "    Camera_Keyframe_Start();\n"
        "    AssertTrue(Camera_Keyframe_IsRunning(), \"rail running\");\n"
        "    Sleep(700.0f);\n"
        "    AssertTrue(!Camera_Keyframe_IsRunning(), \"rail finished\");\n"
        "    Vec3 endPos = Camera_GetPos();\n"
        "    AssertFloat(endPos.x, -5.0f, 0.05f, \"rail end pos\");\n"
        "    Gfx_Flag_Set(\"bloom\", true);\n"
        "    AssertTrue(Gfx_Flag_Get(\"bloom\"), \"gfx flag round-trip\");\n"
        "    Gfx_Flag_Set(\"bloom\", false);\n"
        "    Gfx_SetFogNear(1.5f);\n"
        "    Gfx_SetFogFar(40.0f);\n"
        "    Gfx_SetFogColor(10, 20, 30, 255);\n"
        "    Gfx_SetSunDir(0.3f, -1.0f, 0.2f);\n"
        "    Gfx_SetSunBrightness(1.2f);\n"
        "    Gfx_SetBloomIntensity(0.7f);\n"
        "    Gfx_SetSSAOStrength(1.1f);\n"
        "    Gfx_SetCascadeCount(3);\n"
        "    int m = Model_Add(\"resources/model/button.glb\");\n"
        "    AssertTrue(m >= 0, \"model added\");\n"
        "    if (m >= 0) {\n"
        "        Model_SetPos(m, Vec3_Create(0.0f, 0.0f, 0.0f));\n"
        "        Model_SetScale(m, Vec3_Create(1.0f, 1.0f, 1.0f));\n"
        "        Model_Remove(m);\n"
        "    }\n"
        "    int l = Light_Add();\n"
        "    AssertTrue(l >= 0, \"light added\");\n"
        "    if (l >= 0) {\n"
        "        Light_SetPos(l, Vec3_Create(1.0f, 2.0f, 3.0f));\n"
        "        Light_SetColor(l, 255, 200, 150, 255);\n"
        "        Light_SetBrightness(l, 2.0f);\n"
        "        Light_Remove(l);\n"
        "    }\n"
        "    Sound_Beep(440.0f, 40, 0.2f);\n"
        "    Music_SetVolume(0.5f);\n"
        "    Background_SetScale(2.5f);\n"
        "    AssertFloat(Background_GetScale(), 2.5f, 0.001f, \"bg scale\");\n"
        "    Background_SetRotate(0.75f);\n"
        "    AssertFloat(Background_GetRotate(), 0.75f, 0.001f, \"bg rotate\");\n"
        "    Background_SetMatrixBlend(0.5f);\n"
        "    AssertFloat(Background_GetMatrixBlend(), 0.5f, 0.001f, \"bg matrix blend\");\n"
        "    Background_SetPaletteMode(2);\n"
        "    AssertEqual(Background_GetPaletteMode(), 2, \"bg palette mode\");\n"
        "    Background_SetScaleVel(0.0f);\n"
        "    Background_SetScaleAccel(0.0f);\n"
        "    Background_SetScaleOrigin(0.5f, 0.5f);\n"
        "    Background_SetScaleOriginVel(0.0f, 0.0f);\n"
        "    Background_SetRotateVel(0.0f);\n"
        "    Background_SetRotateAccel(0.0f);\n"
        "    Background_SetRotateOrigin(0.5f, 0.5f);\n"
        "    Background_SetRotateOriginVel(0.0f, 0.0f);\n"
        "    Background_SetTranslate(0.1f, -0.2f);\n"
        "    Background_SetTranslateVel(0.0f, 0.0f);\n"
        "    Background_SetTranslateAccel(0.0f, 0.0f);\n"
        "    Background_SetStretch(1.0f, 1.0f, 1.0f, 1.0f);\n"
        "    Background_SetStretchVel(0.0f, 0.0f, 0.0f, 0.0f);\n"
        "    Background_SetPerspective(0.0f, 0.0f);\n"
        "    Background_SetPerspectiveVel(0.0f, 0.0f);\n"
        "    Background_SetAffine(0.0f, 0.0f);\n"
        "    Background_SetAffineVel(0.0f, 0.0f);\n"
        "    Background_SetMatrixA(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);\n"
        "    Background_SetMatrixB(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);\n"
        "    Background_SetWarp(0.1f, 0.2f, 0.3f);\n"
        "    Background_SetWarpVel(0.0f, 0.0f, 0.0f);\n"
        "    Background_SetWarpExp(2.0f);\n"
        "    Background_SetWaveAmplitude(1.0f, 2.0f, 3.0f, 4.0f);\n"
        "    Background_SetWaveFrequency(0.1f, 0.2f, 0.3f, 0.4f);\n"
        "    Background_SetWaveSpeed(0.01f, 0.02f, 0.03f, 0.04f);\n"
        "    Background_SetWavePhase(0.0f, 0.0f, 0.0f, 0.0f);\n"
        "    Background_SetWaveRegularwave(0.0f, 0.0f, 0.0f, 0.0f);\n"
        "    Background_SetWaveInterlaced(0.0f, 0.0f, 0.0f, 0.0f);\n"
        "    Background_SetWaveInterlacedMultiplier(1.0f, 1.0f, 1.0f, 1.0f);\n"
        "    Background_SetHdmaAmplitude(0, 0.01f);\n"
        "    Background_SetHdmaFrequency(0, 1.0f);\n"
        "    Background_SetHdmaSpeed(0, 0.1f);\n"
        "    Background_SetHdmaPhase(0, 0.0f);\n"
        "    Background_SetHdmaOffset(0, 0.0f, 0.0f);\n"
        "    Background_SetNoiseMode(BG_NOISE_FRACTAL);\n"
        "    Background_SetNoiseScale(1.0f);\n"
        "    Background_SetNoiseSpeed(0.0f);\n"
        "    Background_SetNoiseOctaves(4);\n"
        "    Background_SetNoiseLacunarity(2.0f);\n"
        "    Background_SetNoiseGain(0.5f);\n"
        "    for (int i = 0; i < 2; i++) {\n"
        "        Background_SetSinusoidAmplitude(i, 0.1f, 0.2f, 0.3f, 0.4f);\n"
        "        Background_SetSinusoidFrequency(i, 0.1f, 0.2f, 0.3f, 0.4f);\n"
        "        Background_SetSinusoidPhase(i, 0.0f, 0.0f, 0.0f, 0.0f);\n"
        "        Background_SetSinusoidSpeed(i, 0.01f, 0.02f, 0.03f, 0.04f);\n"
        "        Background_SetSinusoidRegularwave(i, 0.0f, 0.0f, 0.0f, 0.0f);\n"
        "        Background_SetSinusoidInterlaced(i, 0.0f, 0.0f, 0.0f, 0.0f);\n"
        "        Background_SetSinusoidInterlacedMultiplier(i, 1.0f, 1.0f, 1.0f, 1.0f);\n"
        "        Background_SetSinusoidDcOffset(i, 0.0f, 0.0f, 0.0f, 0.0f);\n"
        "        Background_SetSinusoidLinear(i, 0.0f, 0.0f, 0.0f, 0.0f);\n"
        "        Background_SetSinusoidQuadratic(i, 0.0f, 0.0f, 0.0f, 0.0f);\n"
        "        Background_SetSinusoidExponent(i, 1.0f);\n"
        "        Background_SetSinusoidAttenuation(i, 0.0f);\n"
        "        Background_SetSinusoidCenter(i, 0.5f, 0.5f);\n"
        "    }\n"
        "    Background_SetPaletteSize(256.0f);\n"
        "    Background_SetPaletteAmount(16.0f);\n"
        "    Background_SetPaletteSpeed(0.1f);\n"
        "    Background_SetPaletteFrequency(1.0f);\n"
        "    Background_SetPaletteColor1(0.1f, 0.2f, 0.3f, 1.0f);\n"
        "    Background_SetPaletteColor2(0.4f, 0.5f, 0.6f, 1.0f);\n"
        "    Background_SetPaletteColor(0, 1.0f, 0.0f, 0.0f, 1.0f);\n"
        "    Background_SetPaletteBgBoundary(0.1f);\n"
        "    Background_SetPaletteFgBoundary(0.9f);\n"
        "    Background_SetPaletteBgColor(0.0f, 0.0f, 0.0f, 1.0f);\n"
        "    Background_SetPaletteFgColor(1.0f, 1.0f, 1.0f, 1.0f);\n"
        "    Background_SetPaletteRowCount(16);\n"
        "    Background_SetPaletteBlendMode(0);\n"
        "    Background_SetPaletteColorMath(0);\n"
        "    Background_SetPaletteColorMathMask(1.0f, 1.0f, 1.0f);\n"
        "    Background_SetBoundaryXY(-1.0f, 1.0f, -1.0f, 1.0f);\n"
        "    Background_SetBoundaryZ(0.0f, 10.0f);\n"
        "    Background_SetModulo(1.0f, 1.0f);\n"
        "    Background_SetParamEnable(true);\n"
        "    Background_SetWarpEnable(true);\n"
        "    Background_SetWaveEnable(true);\n"
        "    Background_SetPaletteEnable(true);\n"
        "    Background_SetBoundaryEnable(true);\n"
        "    Background_SetBoundaryXYEnable(true);\n"
        "    Background_SetBoundaryZEnable(true);\n"
        "    Background_SetModuloEnable(false);\n"
        "    Background_SetUseTransformMatrix(false);\n"
        "    Background_SetUseDirectMatrix(false);\n"
        "    Background_SetHdmaEnable(false);\n"
        "    Background_SetBattleProjection(false);\n"
        "    Background_React(BG_REACT_FLASH, 0.5f, 120.0f);\n"
        "    Background_SetTexture(\"resources/texture/texture.png\");\n"
        "    int bgWaited = 0;\n"
        "    while (!Background_IsTextureReady() && bgWaited < 120) {\n"
        "        Sleep(50.0f);\n"
        "        bgWaited++;\n"
        "    }\n"
        "    AssertTrue(Background_IsTextureReady(), \"bg texture ready\");\n"
        "    Background_SavePreset(\"" + str(presetPath) + "\");\n"
        "    Background_Transition(\"" + str(presetPath) + "\", 250.0f, BG_TRANS_LINEAR);\n"
        "    AssertTrue(Background_IsTransitionActive(), \"bg transition active\");\n"
        "    Sleep(600.0f);\n"
        "    AssertTrue(!Background_IsTransitionActive(), \"bg transition done\");\n"
        "    Background_TransitionStop();\n"
        "    Background_LoadDefault();\n"
        "    AssertFloat(Background_GetScale(), 1.0f, 0.001f, \"bg default load\");\n"
        "    Background_LoadPreset(\"" + str(presetPath) + "\");\n"
        "    Background_LoadBuiltIn(BG_PRESET_TITLE);\n"
        "    Quit(0);\n"
        "}\n")
    result = subprocess.run(
        [str(BIN), "--script", str(script)], cwd=str(ROOT),
        env=clean_env(GX_FRAMES="300"),
        capture_output=True, text=True, timeout=TIMEOUT)
    log = audit.strip_ansi(result.stdout + result.stderr)
    check("tour: camera + rail + gfx + models/lights + audio + background",
          result.returncode == 0 and "rail end pos" in log
          and "gfx flag round-trip" in log and "model added" in log
          and "light added" in log and "bg scale" in log
          and "bg transition done" in log and "bg default load" in log,
          f"exit {result.returncode}", failures)


def probe_synth_apis(workdir, failures):
    script = workdir / "synth_probe.as"
    presetPath = workdir / "synth_probe_preset.json"
    script.write_text(
        "void main()\n"
        "{\n"
        "    AssertEqual(Synth_GetVoiceCount(), 8, \"synth voice count\");\n"
        "    AssertTrue(Synth_IsMuted(), \"synth muted for gates\");\n"
        "    Synth_SetMuted(false);\n"
        "    AssertTrue(!Synth_IsMuted(), \"synth mute toggle\");\n"
        "    Synth_SetMuted(true);\n"
        "    Synth_SetDefaultWaveform(SYNTH_WAVE_SQUARE);\n"
        "    Synth_SetDefaultADSR(0.01f, 0.05f, 0.6f, 0.08f);\n"
        "    Synth_SetDefaultFilter(SYNTH_FILTER_LOWPASS, 0.5f, 0.2f);\n"
        "    Synth_SetDefaultLFO(5.0f, 0.1f, SYNTH_WAVE_SINE, SYNTH_LFO_AMP);\n"
        "    Synth_SetDefaultFM(SYNTH_FM_SIMPLE, 2.0f, 0.3f, 1.0f, 0.0f, 0.0f);\n"
        "    Synth_SetDefaultPulseWidth(0.5f);\n"
        "    Synth_SetDefaultPortamento(0.0f);\n"
        "    Synth_SetMasterVolume(0.5f);\n"
        "    Synth_SetDelay(0.2f, 0.3f, 0.2f, 0.5f);\n"
        "    Synth_SetDelayEnable(true);\n"
        "    int v = Synth_NoteOn(440.0f, 0.6f);\n"
        "    AssertTrue(v >= 0, \"synth note on\");\n"
        "    AssertTrue(Synth_VoiceActive(v), \"synth voice active\");\n"
        "    Synth_SetPan(v, -1.0f);\n"
        "    Synth_SetADSR(v, 0.01f, 0.05f, 0.5f, 0.08f);\n"
        "    Synth_SetFilter(v, SYNTH_FILTER_LOWPASS, 0.4f, 0.3f);\n"
        "    Synth_SetLFO(v, SYNTH_LFO_AMP, 5.0f, 0.2f, SYNTH_WAVE_SINE);\n"
        "    Synth_SetFM(v, SYNTH_FM_SIMPLE, 2.0f, 0.4f, 1.0f, 0.0f, 0.0f);\n"
        "    Synth_SetPulseWidth(v, 0.25f);\n"
        "    Synth_SetPortamento(v, 0.05f);\n"
        "    Synth_ApplyDefaultsToActive();\n"
        "    Sleep(300.0f);\n"
        "    bool hadDevice = Synth_IsAttached();\n"
        "    if (hadDevice) {\n"
        "        AssertTrue(Synth_ScopeCount() > 0, \"synth scope fed\");\n"
        "    } else {\n"
        "        Print(\"synth: no audio device, mixer assertions skipped\");\n"
        "    }\n"
        "    Synth_NoteOff(v);\n"
        "    Synth_SetDelayEnable(false);\n"
        "    Synth_AllNotesOff();\n"
        "    Sleep(1700.0f);\n"
        "    if (hadDevice) {\n"
        "        AssertTrue(!Synth_IsAttached(), \"synth mixer detached idle\");\n"
        "    }\n"
        "    Synth_Arp_SetMode(SYNTH_ARP_UP);\n"
        "    Synth_Arp_SetRate(0.05f);\n"
        "    Synth_Arp_SetGate(0.5f);\n"
        "    Synth_Arp_NoteOn(330.0f, 0.5f);\n"
        "    Synth_Arp_NoteOn(440.0f, 0.5f);\n"
        "    Sleep(200.0f);\n"
        "    Synth_Arp_NoteOff(330.0f);\n"
        "    Synth_Arp_AllNotesOff();\n"
        "    Synth_Seq_SetBPM(240.0f);\n"
        "    Synth_Seq_SetLength(0, 4);\n"
        "    Synth_Seq_SetStep(0, 0, 330.0f, 0.5f, 0.9f);\n"
        "    Synth_Seq_SetStep(0, 1, 440.0f, 0.5f, 0.9f);\n"
        "    Synth_Seq_Play();\n"
        "    Sleep(200.0f);\n"
        "    Synth_Seq_Stop();\n"
        "    Synth_SetDefaultFilter(SYNTH_FILTER_HIGHPASS, 0.7f, 0.1f);\n"
        "    AssertTrue(Synth_Patch_SaveToBank(0, \"probe\") == 0, \"synth patch save\");\n"
        "    AssertTrue(Synth_PatchCount() >= 1, \"synth patch count\");\n"
        "    Synth_Patch_LoadFromBank(0, 0);\n"
        "    int v2 = Synth_NoteOnPatch(660.0f, 0.5f, 0);\n"
        "    AssertTrue(v2 >= 0, \"synth patch note\");\n"
        "    AssertFloat(Synth_VoiceFilterCutoff(v2), 0.7f, 0.03f, \"synth patch cutoff applied\");\n"
        "    Synth_AllNotesOff();\n"
        "    Synth_WavetableGenerate(0, SYNTH_WAVE_SAWTOOTH);\n"
        "    Synth_SetWavetableIndex(0);\n"
        "    AssertTrue(Synth_WavetableCount() >= 1, \"synth wavetable loaded\");\n"
        "    AssertEqual(Synth_WavetableSize(0), 2048, \"synth wavetable size\");\n"
        "    AssertFloat(Synth_WavetableSample(0, 1024), 0.5f, 0.02f, \"synth wavetable sample\");\n"
        "    AssertTrue(Synth_WavetableLoad(0, \"resources/audio/wavetable/warm_saw.f32\") == 0, \"synth wavetable file load\");\n"
        "    AssertEqual(Synth_WavetableSize(0), 1024, \"synth wavetable file size\");\n"
        "    float warm128 = Synth_WavetableSample(0, 128);\n"
        "    AssertTrue(Synth_WavetableLoad(0, \"resources/audio/wavetable/organ.f32\") == 0, \"synth wavetable organ load\");\n"
        "    float organ128 = Synth_WavetableSample(0, 128);\n"
        "    AssertTrue(warm128 + 0.05f < organ128 || organ128 + 0.05f < warm128, \"synth wavetable tables differ\");\n"
        "    AssertTrue(Synth_WavetableLoad(0, \"resources/audio/wavetable/missing.f32\") == 1, \"synth wavetable bad path\");\n"
        "    int v3 = Synth_NoteOn(330.0f, 0.5f);\n"
        "    AssertTrue(v3 >= 0, \"synth wavetable note\");\n"
        "    Synth_AllNotesOff();\n"
        "    float scope = Synth_ScopeAt(0);\n"
        "    // preset file round trip: distinctive state, save, mutate, load\n"
        "    Synth_SetDefaultFilter(SYNTH_FILTER_LOWPASS, 0.85f, 0.15f);\n"
        "    Synth_SetMasterVolume(0.33f);\n"
        "    AssertTrue(Synth_Preset_Save(\"" + str(presetPath) + "\") == 0, \"synth preset save\");\n"
        "    Synth_SetDefaultFilter(SYNTH_FILTER_BANDPASS, 0.2f, 0.9f);\n"
        "    Synth_SetMasterVolume(0.9f);\n"
        "    AssertTrue(Synth_Preset_Load(\"" + str(presetPath) + "\") == 0, \"synth preset load\");\n"
        "    AssertFloat(Synth_GetDefaultFilterCutoff(), 0.85f, 0.03f, \"synth preset cutoff restored\");\n"
        "    AssertEqual(Synth_GetDefaultFilterMode(), SYNTH_FILTER_LOWPASS, \"synth preset mode restored\");\n"
        "    AssertFloat(Synth_GetMasterVolume(), 0.33f, 0.03f, \"synth preset volume restored\");\n"
        "    Quit(0);\n"
        "}\n")
    result = subprocess.run(
        [str(BIN), "--script", str(script)], cwd=str(ROOT),
        env=clean_env(GX_FRAMES="400"),
        capture_output=True, text=True, timeout=TIMEOUT)
    log = audit.strip_ansi(result.stdout + result.stderr)
    check("tour: synth notes/arp/sequencer/patch/preset",
          result.returncode == 0 and "synth voice count" in log
          and "synth muted for gates" in log and "synth mute toggle" in log
          and "synth note on" in log and "synth voice active" in log
          and "synth patch note" in log and "synth wavetable note" in log
          and "synth patch cutoff applied" in log
          and "synth wavetable sample" in log
          and "synth wavetable size" in log
          and "synth wavetable file load" in log
          and "synth wavetable file size" in log
          and "synth wavetable tables differ" in log
          and "synth wavetable bad path" in log
          and "synth preset cutoff restored" in log
          and "synth preset mode restored" in log
          and "synth preset volume restored" in log,
          f"exit {result.returncode}", failures)


#ai: OPX/PAD engines — notes, operator/layer edits, mute contract, lazy mixer
#   attach/detach (device-gated like the synth probe) and the preset schema
#   round trip. Both engines are muted by default; the probe asserts that and
#   toggles explicitly.
def probe_opx_pad_apis(workdir, failures):
    script = workdir / "opx_pad_probe.as"
    opxPreset = workdir / "opx_probe_preset.json"
    padPreset = workdir / "pad_probe_preset.json"
    script.write_text(
        "void main()\n"
        "{\n"
        "    AssertEqual(Opx_AlgorithmCount(), 8, \"opx algorithm count\");\n"
        "    AssertTrue(Opx_AlgorithmName(OPX_ALG_EP) == \"EP\", \"opx algorithm name\");\n"
        "    AssertTrue(Opx_IsMuted(), \"opx muted for gates\");\n"
        "    Opx_SetMuted(false);\n"
        "    AssertTrue(!Opx_IsMuted(), \"opx mute toggle\");\n"
        "    Opx_SetMuted(true);\n"
        "    Opx_SetDefaultAlgorithm(OPX_ALG_COMPLEX);\n"
        "    AssertEqual(Opx_GetDefaultAlgorithm(), OPX_ALG_COMPLEX, \"opx algorithm set\");\n"
        "    Opx_SetDefaultOperator(0, 2.0f, 0.4f);\n"
        "    AssertFloat(Opx_GetOperatorRatio(0), 2.0f, 0.01f, \"opx operator ratio\");\n"
        "    Opx_SetDefaultOperatorEnabled(0, true);\n"
        "    AssertTrue(Opx_GetOperatorEnabled(0), \"opx operator enabled\");\n"
        "    Opx_SetDefaultOperatorADSR(0, 0.01f, 0.1f, 0.7f, 0.2f);\n"
        "    Opx_SetDefaultOperatorDetune(0, 10.0f);\n"
        "    Opx_SetDefaultOperatorFeedback(0, 0.3f);\n"
        "    int v = Opx_NoteOn(440.0f, 0.7f, OPX_ALG_EP);\n"
        "    AssertTrue(v >= 0, \"opx note on\");\n"
        "    AssertTrue(Opx_VoiceActive(v), \"opx voice active\");\n"
        "    Opx_SetOperator(v, 1, 3.0f, 0.5f);\n"
        "    Opx_SetOperatorEnabled(v, 1, true);\n"
        "    Opx_SetOperatorADSR(v, 1, 0.01f, 0.1f, 0.8f, 0.05f);\n"
        "    Opx_SetOperatorDetune(v, 1, -5.0f);\n"
        "    Opx_SetOperatorFeedback(v, 1, 0.1f);\n"
        "    Opx_SetPan(v, -0.5f);\n"
        "    Sleep(150.0f);\n"
        "    bool opxDevice = Opx_IsAttached();\n"
        "    if (opxDevice) Print(\"opx: device attach seen\");\n"
        "    Opx_AllNotesOff();\n"
        "    Sleep(1700.0f);\n"
        "    if (opxDevice) AssertTrue(!Opx_IsAttached(), \"opx mixer detached idle\");\n"
        "    Opx_SetDefaultOperator(0, 2.5f, 0.6f);\n"
        "    Opx_SetDefaultAlgorithm(OPX_ALG_CASCADE);\n"
        "    AssertTrue(Opx_Preset_Save(\"" + str(opxPreset) + "\") == 0, \"opx preset save\");\n"
        "    Opx_SetDefaultOperator(0, 1.0f, 0.1f);\n"
        "    Opx_SetDefaultAlgorithm(OPX_ALG_ALL_CARRIER);\n"
        "    AssertTrue(Opx_Preset_Load(\"" + str(opxPreset) + "\") == 0, \"opx preset load\");\n"
        "    AssertFloat(Opx_GetOperatorRatio(0), 2.5f, 0.01f, \"opx preset ratio restored\");\n"
        "    AssertFloat(Opx_GetOperatorLevel(0), 0.6f, 0.01f, \"opx preset level restored\");\n"
        "    AssertEqual(Opx_GetDefaultAlgorithm(), OPX_ALG_CASCADE, \"opx preset algorithm restored\");\n"
        "    AssertTrue(Pad_IsMuted(), \"pad muted for gates\");\n"
        "    Pad_SetMuted(false);\n"
        "    AssertTrue(!Pad_IsMuted(), \"pad mute toggle\");\n"
        "    Pad_SetMuted(true);\n"
        "    AssertEqual(Pad_GetLayerWaveform(1), PAD_WAVE_TRI, \"pad default layer waveform\");\n"
        "    Pad_SetDefaultLayer(1, PAD_WAVE_TRI, 25.0f, 0.6f, -0.5f, 0.2f, 10.0f);\n"
        "    AssertFloat(Pad_GetLayerLevel(1), 0.6f, 0.01f, \"pad layer level\");\n"
        "    AssertFloat(Pad_GetLayerPan(1), -0.5f, 0.01f, \"pad layer pan\");\n"
        "    Pad_SetDefaultADSR(1.0f, 1.5f, 0.8f, 0.05f);\n"
        "    Pad_SetDefaultFilter(PAD_FILTER_LP, 0.08f, 0.2f);\n"
        "    Pad_SetDefaultFilterLFO(0.2f, 0.4f);\n"
        "    Pad_SetChorus(true, 0.6f, 0.6f, 0.5f, 0.25f);\n"
        "    Pad_SetWidth(0.9f);\n"
        "    Pad_SetMasterVolume(0.45f);\n"
        "    int pv = Pad_NoteOn(220.0f, 0.8f);\n"
        "    AssertTrue(pv >= 0, \"pad note on\");\n"
        "    AssertTrue(Pad_VoiceActive(pv), \"pad voice active\");\n"
        "    Pad_SetLayer(pv, 0, PAD_WAVE_SAW, -9.0f, 0.5f, -0.6f, 0.1f, 6.0f);\n"
        "    Pad_SetADSR(pv, 0.5f, 1.0f, 0.8f, 0.05f);\n"
        "    Pad_SetFilter(pv, PAD_FILTER_LP, 0.06f, 0.3f);\n"
        "    Pad_SetFilterLFO(pv, 0.15f, 0.3f);\n"
        "    Pad_SetVoicePan(pv, 0.2f);\n"
        "    Sleep(150.0f);\n"
        "    bool padDevice = Pad_IsAttached();\n"
        "    if (padDevice) Print(\"pad: device attach seen\");\n"
        "    Pad_NoteOff(pv);\n"
        "    Pad_AllNotesOff();\n"
        "    Sleep(1700.0f);\n"
        "    if (padDevice) AssertTrue(!Pad_IsAttached(), \"pad mixer detached idle\");\n"
        "    Pad_SetMasterVolume(0.37f);\n"
        "    Pad_SetWidth(0.75f);\n"
        "    AssertTrue(Pad_Preset_Save(\"" + str(padPreset) + "\") == 0, \"pad preset save\");\n"
        "    Pad_SetMasterVolume(0.9f);\n"
        "    Pad_SetWidth(0.1f);\n"
        "    AssertTrue(Pad_Preset_Load(\"" + str(padPreset) + "\") == 0, \"pad preset load\");\n"
        "    AssertFloat(Pad_GetMasterVolume(), 0.37f, 0.01f, \"pad preset volume restored\");\n"
        "    AssertFloat(Pad_GetWidth(), 0.75f, 0.01f, \"pad preset width restored\");\n"
        "    Audio_SetMasterFilter(true, AUDIO_FILTER_LP, 0.35f, 0.2f);\n"
        "    AssertTrue(Audio_GetMasterFilterEnabled(), \"master filter enabled\");\n"
        "    AssertEqual(Audio_GetMasterFilterMode(), AUDIO_FILTER_LP, \"master filter mode\");\n"
        "    AssertFloat(Audio_GetMasterFilterCutoff(), 0.35f, 0.01f, \"master filter cutoff\");\n"
        "    AssertFloat(Audio_GetMasterFilterResonance(), 0.2f, 0.01f, \"master filter resonance\");\n"
        "    Audio_SetMasterFilter(false, AUDIO_FILTER_LP, 0.8f, 0.0f);\n"
        "    AssertTrue(!Audio_GetMasterFilterEnabled(), \"master filter disabled\");\n"
        "    Quit(0);\n"
        "}\n")
    result = subprocess.run(
        [str(BIN), "--script", str(script)], cwd=str(ROOT),
        env=clean_env(GX_FRAMES="400"),
        capture_output=True, text=True, timeout=TIMEOUT)
    log = audit.strip_ansi(result.stdout + result.stderr)
    check("tour: opx/pad notes/edits/presets",
          result.returncode == 0 and "opx algorithm count" in log
          and "opx algorithm name" in log
          and "opx muted for gates" in log and "opx mute toggle" in log
          and "opx algorithm set" in log and "opx operator ratio" in log
          and "opx operator enabled" in log and "opx note on" in log
          and "opx voice active" in log
          and "opx preset ratio restored" in log
          and "opx preset level restored" in log
          and "opx preset algorithm restored" in log
          and "pad muted for gates" in log and "pad mute toggle" in log
          and "pad default layer waveform" in log and "pad layer level" in log
          and "pad layer pan" in log and "pad note on" in log
          and "pad voice active" in log
          and "pad preset volume restored" in log
          and "pad preset width restored" in log
          and "master filter enabled" in log
          and "master filter mode" in log
          and "master filter cutoff" in log
          and "master filter resonance" in log
          and "master filter disabled" in log,
          f"exit {result.returncode}", failures)


#ai: OPX/PAD demo — the self-test covers the combined script surface (notes,
#   mute, per-voice edits, toggles, algorithm switch, preset round trips);
#   the interactive boot smoke runs one frame-capped session like the synth
#   demo does.
def probe_opx_pad_demo(workdir, failures):
    presetBase = workdir / "opx_pad_demo"
    seed = workdir / "opx_pad_selftest_seed.as"
    seed.write_text(
        "void main()\n"
        "{\n"
        "    ScriptSetInt(\"opx_pad_selftest\", 1);\n"
        "    ScriptSetString(\"opx_pad_preset_path\", \"%s\");\n"
        "    Quit(0);\n"
        "}\n" % presetBase)
    statePath = workdir / "opx_pad_state.json"
    env = clean_env(GX_FRAMES="60", GX_SCRIPT_STATE=str(statePath))
    first = subprocess.run([str(BIN), "--script", str(seed)], cwd=str(ROOT),
                           env=env, capture_output=True, text=True, timeout=TIMEOUT)
    demo = subprocess.run(
        [str(BIN), "--script", "resources/script/audio/opx_pad_demo.as"],
        cwd=str(ROOT), env=env, capture_output=True, text=True, timeout=TIMEOUT)
    log = audit.strip_ansi(demo.stdout + demo.stderr)
    check("opx/pad demo: self-test (notes/edits/mute/toggles/presets)",
          first.returncode == 0 and demo.returncode == 0
          and "opx/pad demo: selftest ok" in log
          and "opx/pad demo: algorithm count" in log
          and "opx/pad demo: opx muted by default" in log
          and "opx/pad demo: pad muted by default" in log
          and "opx/pad demo: mute toggle" in log
          and "opx/pad demo: opx note on" in log
          and "opx/pad demo: opx voice active" in log
          and "opx/pad demo: pad note on" in log
          and "opx/pad demo: pad voice active" in log
          and "opx/pad demo: chorus toggle" in log
          and "opx/pad demo: width toggle" in log
          and "opx/pad demo: algorithm switch" in log
          and "opx/pad demo: opx preset ratio restored" in log
          and "opx/pad demo: opx preset algorithm restored" in log
          and "opx/pad demo: pad preset layer restored" in log
          and "opx/pad demo: pad preset width restored" in log
          and (presetBase.parent / "opx_pad_demo_opx.json").is_file()
          and (presetBase.parent / "opx_pad_demo_pad.json").is_file(),
          f"exit {demo.returncode}", failures)

    play = subprocess.run(
        [str(BIN), "--script", "resources/script/audio/opx_pad_demo.as"],
        cwd=str(ROOT),
        env=clean_env(GX_FRAMES="60",
                      GX_SCRIPT_STATE=str(workdir / "opx_pad_play_state.json")),
        capture_output=True, text=True, timeout=TIMEOUT)
    check("opx/pad demo: interactive boot smoke (frames cap)",
          play.returncode == 0,
          f"exit {play.returncode}", failures)


def probe_camera_rail(workdir, failures):
    #opencode: the rail is shell-owned (module-side); this probe covers the
    #   script API persistence + the module-drawn editor open path. The guard
    #   target directory exists so a broken guard would actually write there
    #   and the "not created" assert would catch it.
    guardDir = workdir / "resources" / "tools" / "camera"
    guardDir.mkdir(parents=True, exist_ok=True)
    guardPath = guardDir / "guard.json"
    railPath = workdir / "camera_rail_probe.json"
    script = workdir / "camera_rail_probe.as"
    script.write_text(
        "void main()\n"
        "{\n"
        "    Camera_Keyframe_Clear();\n"
        "    Camera_SetPos(Vec3(1.0f, 2.0f, 3.0f));\n"
        "    Camera_SetTarget(Vec3(0.0f, 0.0f, 0.0f));\n"
        "    Camera_SetFov(60.0f);\n"
        "    Camera_Rail_AddGrab(2.0f);\n"
        "    Camera_SetPos(Vec3(4.0f, 5.0f, 6.0f));\n"
        "    Camera_Rail_AddGrab(3.0f);\n"
        "    AssertEqual(Camera_Rail_Count(), 2, \"rail count after grabs\");\n"
        "    AssertTrue(Camera_Rail_Save(\"" + str(railPath) + "\") == 0, \"rail saved\");\n"
        "    Camera_Keyframe_Clear();\n"
        "    AssertEqual(Camera_Rail_Count(), 0, \"rail cleared\");\n"
        "    AssertTrue(Camera_Rail_Load(\"" + str(railPath) + "\") == 0, \"rail loaded\");\n"
        "    AssertEqual(Camera_Rail_Count(), 2, \"rail count after load\");\n"
        "    AssertTrue(Camera_Rail_Save(\"" + str(guardPath) + "\") == 1, \"shipped preset refused\");\n"
        "    Quit(0);\n"
        "}\n")
    result = subprocess.run(
        [str(BIN), "--script", str(script)], cwd=str(ROOT),
        env=clean_env(GX_FRAMES="300"),
        capture_output=True, text=True, timeout=TIMEOUT)
    log = audit.strip_ansi(result.stdout + result.stderr)
    check("tour: camera rail grab/save/load round trip + guard",
          result.returncode == 0
          and "rail count after grabs" in log
          and "rail saved" in log
          and "rail count after load" in log
          and "shipped preset refused" in log
          and railPath.is_file()
          and "camera_rail" in railPath.read_text()
          and not guardPath.exists(),
          f"exit {result.returncode}", failures)

    #opencode: the editor is a module-drawn panel — the exe opener only sets
    #   the flag the gx_shell tools-draw hook reads.
    result = subprocess.run(
        [str(BIN), "--screenWidth", "1024", "--screenHeight", "768"],
        cwd=str(ROOT), env=clean_env(GX_FRAMES="40", GX_TOOLS_OPEN="rail"),
        capture_output=True, text=True, timeout=TIMEOUT)
    log = audit.strip_ansi(result.stdout + result.stderr)
    check("module: rail editor opens (GX_TOOLS_OPEN=rail)",
          result.returncode == 0 and "Tools: rail editor opened at runtime" in log,
          f"exit {result.returncode}", failures)


def probe_tour_demo(workdir, failures):
    #opencode: the shipped rail tour demo — loads the sample rail, plays it,
    #   exits when the rail finishes (dogfoods Camera_Rail_Load + the rail
    #   editor's format end to end).
    result = subprocess.run(
        [str(BIN), "--script", "resources/script/tour/tour_demo.as"], cwd=str(ROOT),
        env=clean_env(GX_FRAMES="3000"),
        capture_output=True, text=True, timeout=240)
    log = audit.strip_ansi(result.stdout + result.stderr)
    check("tour: rail demo (load/play/finish)",
          result.returncode == 0
          and "tour demo: rail loaded" in log
          and "tour demo: rail keyframes" in log
          and "tour demo: rail finished" in log,
          f"exit {result.returncode}", failures)


def probe_textwriter_apis(workdir, failures):
    script = workdir / "textwriter_probe.as"
    choicesPath = workdir / "tw_choices.txt"
    choicesPath.write_text(
        "intro\n"
        "<choice><option line='3'>Alpha</option><option line='2'>Beta</option></choice>\n"
        "beta target\n"
        "alpha target\n")
    script.write_text(
        "void main()\n"
        "{\n"
        "    SwitchScene(SCENE_RENDERER_TEXTWRITER);\n"
        "    Sleep(250.0f);\n"
        "    AssertTrue(TextWriter_IsLoaded(), \"tw file auto-loaded\");\n"
        "    AssertTrue(TextWriter_GetLineCount() > 0, \"tw line count\");\n"
        "    TextWriter_JumpLine(2);\n"
        "    AssertEqual(TextWriter_GetLine(), 2, \"tw jump line\");\n"
        "    TextWriter_NextLine();\n"
        "    AssertEqual(TextWriter_GetLine(), 3, \"tw next line\");\n"
        "    AssertTrue(TextWriter_GetLineText(0) != \"\", \"tw line text\");\n"
        "    TextWriter_LoadFileTXT(\"" + str(choicesPath) + "\");\n"
        "    AssertTrue(TextWriter_IsLoaded(), \"tw choice file loaded\");\n"
        "    TextWriter_NextLine();\n"
        "    AssertTrue(TextWriter_HasChoices(), \"tw choices present\");\n"
        "    AssertEqual(TextWriter_GetChoiceCount(), 2, \"tw choice count\");\n"
        "    AssertTrue(TextWriter_GetChoiceText(0) == \"Alpha\", \"tw choice text zero\");\n"
        "    AssertEqual(TextWriter_SelectChoice(1), 0, \"tw choice select\");\n"
        "    AssertEqual(TextWriter_GetLine(), 2, \"tw choice jump\");\n"
        "    AssertTrue(!TextWriter_HasChoices(), \"tw choice cleared\");\n"
        "    TextWriter_SetReveal(0.0f);\n"
        "    AssertFloat(TextWriter_GetReveal(), 0.0f, 0.001f, \"tw reveal set\");\n"
        "    TextWriter_SetRevealSpeed(0.5f);\n"
        "    AssertFloat(TextWriter_GetRevealSpeed(), 0.5f, 0.001f, \"tw reveal speed\");\n"
        "    Sleep(250.0f);\n"
        "    AssertTrue(TextWriter_GetReveal() > 0.0f, \"tw reveal advances\");\n"
        "    TextWriter_SetReveal(99999.0f);\n"
        "    AssertTrue(TextWriter_IsRevealComplete(), \"tw reveal complete\");\n"
        "    TextWriter_SetFont(\"resources/font/KenneyMiniSquareMono.ttf\");\n"
        "    AssertTrue(TextWriter_HasFont(), \"tw font loaded\");\n"
        "    AssertTrue(TextWriter_MeasureText(\"Hello\", 32.0f) > 0.0f, \"tw measure text\");\n"
        "    AssertTrue(TextWriter_MeasureTagged(\"<red>Red</> text\", 32.0f) > 0.0f, \"tw measure tagged\");\n"
        "    TextWriter_SetColor(255, 255, 255, 255);\n"
        "    TextWriter_DrawTagged(\"<red>Red</> and <b>bold</>\", 40.0f, 40.0f, 24.0f);\n"
        "    AssertEqual(TextWriter_GetDrawCount(), 1, \"tw draw queued\");\n"
        "    TextWriter_DrawTagged(\"second\", 40.0f, 80.0f, 24.0f);\n"
        "    AssertEqual(TextWriter_GetDrawCount(), 2, \"tw draw count\");\n"
        "    Sleep(150.0f);\n"
        "    TextWriter_ClearDraws();\n"
        "    AssertEqual(TextWriter_GetDrawCount(), 0, \"tw draws cleared\");\n"
        "    TextWriter_ResetFont();\n"
        "    AssertTrue(!TextWriter_HasFont(), \"tw font reset\");\n"
        "    TextWriter_UnloadFile();\n"
        "    AssertTrue(!TextWriter_IsLoaded(), \"tw file unloaded\");\n"
        "    Quit(0);\n"
        "}\n")
    result = subprocess.run(
        [str(BIN), "--script", str(script)], cwd=str(ROOT),
        env=clean_env(GX_FRAMES="300"),
        capture_output=True, text=True, timeout=TIMEOUT)
    log = audit.strip_ansi(result.stdout + result.stderr)
    check("tour: textwriter file/lines/reveal/fonts/choices/tagged draw",
          result.returncode == 0 and "tw file auto-loaded" in log
          and "tw jump line" in log and "tw reveal advances" in log
          and "tw choices present" in log and "tw choice count" in log
          and "tw choice select" in log and "tw choice jump" in log
          and "tw choice cleared" in log
          and "tw measure tagged" in log and "tw draw queued" in log
          and "tw draws cleared" in log and "tw file unloaded" in log,
          f"exit {result.returncode}", failures)


def probe_overlay_apis(workdir, failures):
    script = workdir / "overlay_probe.as"
    script.write_text(
        "void main()\n"
        "{\n"
        "    Overlay_Clear();\n"
        "    AssertEqual(Overlay_Count(), 0, \"overlay empty\");\n"
        "    AssertTrue(Overlay_Width() > 0.0f && Overlay_Height() > 0.0f, \"overlay size\");\n"
        "    AssertTrue(Overlay_TextWidth(\"Hello\", 24.0f) > 0.0f, \"overlay measure\");\n"
        "    Overlay_SetColor(255, 255, 255, 255);\n"
        "    Overlay_Text(40.0f, 30.0f, 24.0f, \"probe\");\n"
        "    Overlay_Rect(40.0f, 80.0f, 120.0f, 40.0f);\n"
        "    Overlay_RectBorder(200.0f, 80.0f, 120.0f, 40.0f);\n"
        "    Overlay_Bar(40.0f, 140.0f, 200.0f, 16.0f, 0.5f);\n"
        "    Overlay_Line(40.0f, 180.0f, 240.0f, 220.0f);\n"
        "    Overlay_Circle(300.0f, 200.0f, 24.0f);\n"
        "    Overlay_Triangle(340.0f, 240.0f, 380.0f, 180.0f, 420.0f, 240.0f);\n"
        "    AssertEqual(Overlay_Count(), 7, \"overlay count\");\n"
        "    Sleep(200.0f);\n"
        "    Overlay_Clear();\n"
        "    AssertEqual(Overlay_Count(), 0, \"overlay cleared\");\n"
        "    Quit(0);\n"
        "}\n")
    result = subprocess.run(
        [str(BIN), "--script", str(script)], cwd=str(ROOT),
        env=clean_env(GX_FRAMES="120"),
        capture_output=True, text=True, timeout=TIMEOUT)
    log = audit.strip_ansi(result.stdout + result.stderr)
    check("tour: overlay paint list (text/rect/bar/shapes)",
          result.returncode == 0 and "overlay size" in log
          and "overlay measure" in log and "overlay count" in log
          and "overlay cleared" in log,
          f"exit {result.returncode}", failures)


def probe_raymarch_apis(workdir, failures):
    script = workdir / "raymarch_probe.as"
    presetPath = workdir / "rm_probe.json"
    script.write_text(
        "void main()\n"
        "{\n"
        "    Raymarch_SetTerrainOffset(1.0f, -2.0f, 3.0f);\n"
        "    Raymarch_SetTerrainScale(1.0f, 1.0f, 1.0f);\n"
        "    Raymarch_SetTerrainSeed(7);\n"
        "    Raymarch_SetTerrainColor(120, 200, 120, 255);\n"
        "    Raymarch_SetTerrainColorSteep(100, 90, 80, 255);\n"
        "    Raymarch_SetTerrainColorBeach(230, 210, 160, 255);\n"
        "    Raymarch_SetTerrainParams(6.0f, 0.002f, 3.0f, 0.0f);\n"
        "    Raymarch_SetWaterOffset(0.0f, -5.0f, 0.0f);\n"
        "    Raymarch_SetWaterScale(1.0f, 1.0f, 1.0f);\n"
        "    Raymarch_SetWaterSpeed(0.1f, 0.0f, 0.05f);\n"
        "    Raymarch_SetWaterColor(40, 90, 160, 255);\n"
        "    Raymarch_SetWaterRipple(1.0f, 0.5f, 0.5f, 0.2f);\n"
        "    Raymarch_SetWaterWhirlpool(0.0f, 0.0f, 0.0f, 0.0f);\n"
        "    Raymarch_SetFogNear(10.0f);\n"
        "    Raymarch_SetFogFar(300.0f);\n"
        "    Raymarch_SetFogColor(200, 210, 230, 255);\n"
        "    Raymarch_SetSunBrightness(1.5f);\n"
        "    Raymarch_SetSunSize(30.0f);\n"
        "    Raymarch_SetSunDir(0.3f, -0.4f, 0.2f);\n"
        "    Raymarch_SetSunColor(255, 240, 200, 255);\n"
        "    Raymarch_SetBounces(2);\n"
        "    Raymarch_SetSteps(64);\n"
        "    Raymarch_SetStepsPerBounce(0);\n"
        "    Raymarch_SetNear(0.1f);\n"
        "    Raymarch_SetFar(400.0f);\n"
        "    Raymarch_SetEpsilon(0.001f);\n"
        "    Raymarch_SetStepMax(4.0f);\n"
        "    Raymarch_SetDistTotal(0.0f);\n"
        "    Raymarch_SetRenderScale(0.75f);\n"
        "    Raymarch_SetFlagTerrain(true);\n"
        "    Raymarch_SetFlagWater(true);\n"
        "    Raymarch_SetFlagFog(true);\n"
        "    Raymarch_SetFlagSky(true);\n"
        "    Raymarch_SetFlagAO(true);\n"
        "    Raymarch_SetFlagObjects(true);\n"
        "    Raymarch_SetFlagLights(true);\n"
        "    Raymarch_SetFlagSunShadows(false);\n"
        "    Raymarch_SetFlagObjectGlow(false);\n"
        "    Raymarch_SetFlagLightShadows(false);\n"
        "    Raymarch_SetFlagLightGlow(false);\n"
        "    Raymarch_SetFlagLightIllumination(true);\n"
        "    Raymarch_SetFlagFrag(false);\n"
        "    Raymarch_SetFlagTerrainGrid(false);\n"
        "    Raymarch_SetFlagSampleSign(false);\n"
        "    Raymarch_SetFlagSampleLastStep(false);\n"
        "    Raymarch_SetFlagProceduralSky(false);\n"
        "    Raymarch_SetFlagClouds(false);\n"
        "    Raymarch_SetFlagProxy(false);\n"
        "    Raymarch_SetFlagVolumetricFog(false);\n"
        "    Raymarch_SetObjectAmount(2);\n"
        "    Raymarch_SetObjectType(0, 1);\n"
        "    Raymarch_SetObjectPos(0, 0.0f, 2.0f, 0.0f);\n"
        "    Raymarch_SetObjectColor(0, 0.8f, 0.3f, 0.2f, 1.0f);\n"
        "    Raymarch_SetObjectParams(0, 1.0f, 0.5f, 0.0f, 0.0f);\n"
        "    Raymarch_SetObjectScale(0, 1.5f, 1.5f, 1.5f);\n"
        "    Raymarch_SetObjectRotate(0, 0.0f, 45.0f, 0.0f);\n"
        "    Raymarch_SetObjectType(1, 2);\n"
        "    Raymarch_RemoveObject(1);\n"
        "    Raymarch_SetLightAmount(1);\n"
        "    Raymarch_SetLightPos(0, 2.0f, 3.0f, -1.0f);\n"
        "    Raymarch_SetLightColor(0, 1.0f, 0.4f, 0.2f, 1.0f);\n"
        "    Raymarch_SetLightBrightness(0, 2.0f);\n"
        "    Raymarch_SetAmbientColor(30, 30, 40, 255);\n"
        "    Raymarch_SetNullColor(0, 0, 0, 0);\n"
        "    Raymarch_SetTime(0.0f);\n"
        "    Raymarch_SetUseInternalTime(true);\n"
        "    Raymarch_SetUseInternalCamera(false);\n"
        "    Raymarch_LoadBuiltIn(RM_PRESET_ISLAND);\n"
        "    Raymarch_LoadBuiltIn(RM_PRESET_WATER);\n"
        "    Raymarch_LoadBuiltIn(RM_PRESET_DESERT);\n"
        "    Raymarch_LoadDefault();\n"
        "    Raymarch_LoadRandom();\n"
        "    Raymarch_SavePreset(\"" + str(presetPath) + "\");\n"
        "    Raymarch_LoadPreset(\"" + str(presetPath) + "\");\n"
        "    AssertTrue(true, \"raymarch api ok\");\n"
        "    Quit(0);\n"
        "}\n")
    result = subprocess.run(
        [str(BIN), "--script", str(script)], cwd=str(ROOT),
        env=clean_env(GX_FRAMES="120"),
        capture_output=True, text=True, timeout=TIMEOUT)
    log = audit.strip_ansi(result.stdout + result.stderr)
    check("tour: raymarch params/flags/objects/presets",
          result.returncode == 0 and "raymarch api ok" in log
          and presetPath.is_file(),
          f"exit {result.returncode}, preset={presetPath.is_file()}", failures)


def probe_pp_apis(workdir, failures):
    script = workdir / "pp_probe.as"
    script.write_text(
        "void main()\n"
        "{\n"
        "    Sleep(250.0f);\n"
        "    PostProcess_SetEnabled(true);\n"
        "    AssertTrue(PostProcess_IsEnabled(), \"pp enabled\");\n"
        "    PostProcess_PushShader(PP_VIGNETTE);\n"
        "    PostProcess_PushShader(PP_PIXELATION);\n"
        "    Sleep(80.0f);\n"
        "    AssertEqual(PostProcess_GetShaderCount(), 2, \"pp chain push\");\n"
        "    PostProcess_MoveShader(1, -1);\n"
        "    PostProcess_ReloadShader(0);\n"
        "    PostProcess_RemoveShader(0);\n"
        "    Sleep(80.0f);\n"
        "    AssertEqual(PostProcess_GetShaderCount(), 1, \"pp chain remove\");\n"
        "    PostProcess_PopShader();\n"
        "    Sleep(80.0f);\n"
        "    AssertEqual(PostProcess_GetShaderCount(), 0, \"pp chain pop\");\n"
        "    PostProcess_ClearShaders();\n"
        "    AssertEqual(PostProcess_GetShaderCount(), 0, \"pp chain clear\");\n"
        "    PostProcess_LoadPreset(PP_PRESET_VIGNETTE);\n"
        "    AssertTrue(true, \"pp preset loaded\");\n"
        "    PostProcess_SetFeedback(true);\n"
        "    PostProcess_SetFeedbackDecay(0.5f);\n"
        "    PostProcess_SetFeedbackBlend(PP_BLEND_ADDITIVE);\n"
        "    PostProcess_SetInject(true);\n"
        "    PostProcess_SetInjectAmount(1);\n"
        "    PostProcess_SetInjectTexture(0, \"resources/texture/gx.png\");\n"
        "    PostProcess_SetInjectCanvas(true);\n"
        "    PostProcess_SetInjectCanvasBlend(PP_BLEND_ALPHA);\n"
        "    PostProcess_SetInjectCanvasTint(255, 255, 255, 255);\n"
        "    PostProcess_ClearInjectTexture(0);\n"
        "    Sleep(200.0f);\n"
        "    PostProcess_Stop();\n"
        "    AssertTrue(!PostProcess_IsEnabled(), \"pp stopped\");\n"
        "    Quit(0);\n"
        "}\n")
    result = subprocess.run(
        [str(BIN), "--script", str(script)], cwd=str(ROOT),
        env=clean_env(GX_FRAMES="200"),
        capture_output=True, text=True, timeout=TIMEOUT)
    log = audit.strip_ansi(result.stdout + result.stderr)
    check("tour: postprocess chain/feedback/inject/presets",
          result.returncode == 0 and "pp enabled" in log
          and "pp chain push" in log and "pp chain clear" in log
          and "pp preset loaded" in log and "pp stopped" in log,
          f"exit {result.returncode}", failures)


def probe_synth_demo(workdir, failures):
    presetPath = workdir / "synth_demo_preset.json"
    seed = workdir / "synth_selftest_seed.as"
    seed.write_text(
        "void main()\n"
        "{\n"
        "    ScriptSetInt(\"synth_selftest\", 1);\n"
        "    ScriptSetString(\"synth_preset_path\", \"%s\");\n"
        "    Quit(0);\n"
        "}\n" % presetPath)
    statePath = workdir / "synth_state.json"
    env = clean_env(GX_FRAMES="60", GX_SCRIPT_STATE=str(statePath))
    first = subprocess.run([str(BIN), "--script", str(seed)], cwd=str(ROOT),
                           env=env, capture_output=True, text=True, timeout=TIMEOUT)
    demo = subprocess.run([str(BIN), "--script", "resources/script/synth/synth.as"],
                          cwd=str(ROOT), env=env, capture_output=True, text=True,
                          timeout=TIMEOUT)
    log = audit.strip_ansi(demo.stdout + demo.stderr)
    check("synth demo: self-test (notes/hit-tests/mute)",
          first.returncode == 0 and demo.returncode == 0
          and "synth demo: selftest ok" in log
          and "synth demo: mute toggle" in log
          and "synth demo: white hit test" in log
          and "synth demo: arp run" in log,
          f"exit {demo.returncode}", failures)
    check("synth demo: wavetable readback + patch/preset round trip",
          demo.returncode == 0
          and "synth demo: wavetable saw mid" in log
          and "synth demo: wavetable waveform active" in log
          and "synth demo: wavetable file loaded" in log
          and "synth demo: wavetable file size" in log
          and "synth demo: file table replaces generated data" in log
          and "synth demo: wavetable bad path reported" in log
          and "synth demo: patch bank round-trip" in log
          and "synth demo: preset cutoff restored" in log
          and "synth demo: preset engine state restored" in log
          and presetPath.is_file(),
          f"exit {demo.returncode}", failures)

    play = subprocess.run([str(BIN), "--script", "resources/script/synth/synth.as"],
                          cwd=str(ROOT),
                          env=clean_env(GX_FRAMES="60",
                                        GX_SCRIPT_STATE=str(workdir / "synth_play_state.json")),
                          capture_output=True, text=True, timeout=TIMEOUT)
    plog = audit.strip_ansi(play.stdout + play.stderr)
    check("synth demo: interactive boot smoke (frames cap)",
          play.returncode == 0 and "failed to add" not in plog
          and "Build() failed" not in plog,
          f"exit {play.returncode}", failures)


def probe_markup_bridge(workdir, failures):
    handler = workdir / "click_handler.as"
    handler.write_text(
        "void OnClick()\n"
        "{\n"
        "    AssertTrue(true, \"onclick fired\");\n"
        "}\n")
    markup = workdir / "script_smoke.json"
    markup.write_text(
        "{\n"
        "  \"name\": \"script_smoke\",\n"
        "  \"description\": \"scripting markup bridge probe\",\n"
        "  \"css\": \"\",\n"
        f"  \"script\": \"{handler}\",\n"
        "  \"templateID\": 0,\n"
        "  \"elements\": [\n"
        "    {\n"
        "      \"type\": \"button\",\n"
        "      \"name\": \"hello\",\n"
        "      \"class\": \"\", \"style\": \"\",\n"
        "      \"gx\": 2.8, \"gy\": 3.4, \"gw\": 2.4, \"gh\": 0.5,\n"
        "      \"color\": [50, 40, 80, 180],\n"
        "      \"text\": \"SCRIPT SMOKE\",\n"
        "      \"fontSize\": 0, \"texture\": \"\", \"shader\": \"\",\n"
        "      \"display\": \"block\", \"layer\": 0, \"visible\": true,\n"
        "      \"centerX\": false, \"centerY\": false,\n"
        "      \"onclick\": \"OnClick\",\n"
        "      \"children\": []\n"
        "    }\n"
        "  ]\n"
        "}\n")
    result = subprocess.run(
        [str(BIN), "--tools", str(markup)],
        cwd=str(ROOT), env=clean_env(GX_FRAMES="30"),
        capture_output=True, text=True, timeout=TIMEOUT)
    log = audit.strip_ansi(result.stdout + result.stderr)
    check("markup: companion script lazy-loaded",
          f"added script '{handler}'" in log,
          "no lazy-load line", failures)


def main():
    if not BIN.is_file():
        print("SCRIPTING_CHECK: missing build/scripting/gX_build — run "
              "make check-scripting", file=sys.stderr)
        return 1

    SCRATCH.mkdir(parents=True, exist_ok=True)
    failures = []
    print("SCRIPTING_CHECK: opt-in shell probes")
    with tempfile.TemporaryDirectory(prefix="scripting_check_",
                                     dir=str(SCRATCH)) as temporary:
        workdir = Path(temporary)
        probe_module(workdir, failures)
        probe_pack(workdir, failures)
        probe_fail(workdir, failures)
        probe_sleep(workdir, failures)
        probe_scene_hooks(workdir, failures)
        probe_hot_reload(workdir, failures)
        probe_scan(workdir, failures)
        probe_mod_scan(workdir, failures)
        probe_dom_click(workdir, failures)
        probe_debugger(workdir, failures)
        probe_state_persistence(workdir, failures)
        probe_script_panel(workdir, failures)
        probe_console_exec(workdir, failures)
        probe_console_script(workdir, failures)
        probe_console_panel(workdir, failures)
        probe_tour_apis(workdir, failures)
        probe_camera_rail(workdir, failures)
        probe_tour_demo(workdir, failures)
        probe_synth_apis(workdir, failures)
        probe_opx_pad_apis(workdir, failures)
        probe_textwriter_apis(workdir, failures)
        probe_overlay_apis(workdir, failures)
        probe_raymarch_apis(workdir, failures)
        probe_pp_apis(workdir, failures)
        probe_synth_demo(workdir, failures)
        probe_opx_pad_demo(workdir, failures)
        probe_markup_bridge(workdir, failures)

    if failures:
        print(f"SCRIPTING_CHECK: FAIL ({len(failures)}): {', '.join(failures)}")
        return 1
    print("SCRIPTING_CHECK: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())

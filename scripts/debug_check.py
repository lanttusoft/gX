#!/usr/bin/env python3
#opencode: debugger gate. Two legs:
#   1. crash trace — a deliberate fault (GX_DEBUG_CRASH_TEST) must exit with
#      128+SIGSEGV and print the header, the register block, a stack with
#      frames and the addr2line recipe;
#   2. variable trace — a built-in watch (GX_DEBUG_WATCH_TEST) must register
#      and report strictly increasing frameCount changes.

import json
import re
import tempfile
from pathlib import Path
import subprocess
import sys

import audit_common as audit


ROOT = audit.ROOT
BIN = audit.BIN
SIGSEGV_EXIT = 128 + 11
SIGABRT_EXIT = 128 + 6
RIP_RE = re.compile(r"RIP=([0-9a-f]{8,})")
STACK_RE = re.compile(r"STACK \((\d+) frames\)")
WATCH_RE = re.compile(r"GX_WATCH: frameCount=(\d+) changed")


def crash_leg():
    environment = audit.scene_env({
        "GX_FIXED_DT": "0.0166666667",
        "GX_FRAMES": "30",
        "GX_DEBUG_CRASH_TEST": "10",
    }, clean=True)
    with audit.settings_preserved():
        result = subprocess.run(
            [str(BIN), "--gfx"], cwd=ROOT, env=environment, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
    output = audit.strip_ansi(result.stdout or "")
    if result.returncode != SIGSEGV_EXIT:
        raise RuntimeError(
            f"crash exit {result.returncode} != {SIGSEGV_EXIT}")
    if "Caught signal 11" not in output:
        raise RuntimeError("crash header missing")
    if not RIP_RE.search(output):
        raise RuntimeError("register block missing")
    stack = STACK_RE.search(output)
    if not stack or int(stack.group(1)) < 3:
        raise RuntimeError(f"stack too short: {stack}")
    if "addr2line" not in output:
        raise RuntimeError("addr2line recipe missing")
    stack_text = output.split("STACK (", 1)[-1]
    if "gX_Debug_Update" not in stack_text:
        raise RuntimeError("crash stack has no symbol names")
    tail = re.search(r"LOG TAIL \((\d+) lines\):\n((?:  .*\n){3,})", output)
    if not tail:
        raise RuntimeError("crash context log ring missing")
    if int(tail.group(1)) < 3:
        raise RuntimeError(f"log ring too short: {tail.group(1)}")
    return int(stack.group(1))


def watchdog_leg():
    #opencode: GX_FRAME_WATCHDOG_MS=1 is below any real frame, so every frame
    #   flags: the overrun logs, then the zone dump and the log tail. The
    #   abort variant must crash with the full context.
    environment = audit.scene_env({
        "GX_FIXED_DT": "0.0166666667",
        "GX_FRAMES": "40",
        "GX_FRAME_WATCHDOG_MS": "1",
    }, clean=True)
    with audit.settings_preserved():
        result = audit.run_scene("--gfx", environment, width=None, height=None,
            fps=None, frames=40, timeout=120, check=True)
    output = audit.strip_ansi(result.stdout or "")
    if "GX_WATCHDOG: frame" not in output:
        raise RuntimeError("watchdog did not flag the overrun")
    if "zone dump follows" not in output:
        raise RuntimeError("watchdog dump missing")
    if "GX_PROFILE: frame=" not in output:
        raise RuntimeError("watchdog did not dump the profiler")
    if "LOG TAIL" not in output:
        raise RuntimeError("watchdog lost the log tail")
    fatal = audit.scene_env({
        "GX_FIXED_DT": "0.0166666667",
        "GX_FRAMES": "40",
        "GX_FRAME_WATCHDOG_MS": "1",
        "GX_FRAME_WATCHDOG_ABORT": "1",
    }, clean=True)
    with audit.settings_preserved():
        result = subprocess.run([str(BIN), "--gfx"], cwd=ROOT, env=fatal,
            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            timeout=120)
    if result.returncode != SIGABRT_EXIT:
        raise RuntimeError(
            f"watchdog abort exit {result.returncode} != {SIGABRT_EXIT}")
    fatal_output = audit.strip_ansi(result.stdout or "")
    if "Caught signal 6" not in fatal_output:
        raise RuntimeError("watchdog abort produced no crash dump")
    return True


def assert_leg():
    #opencode: GX_ASSERT must log the expression/file/line/function, set the
    #   error slot (name=assert) and, under GX_ASSERT_FATAL=1, abort so the
    #   crash handler captures the full trace.
    environment = audit.scene_env({
        "GX_FIXED_DT": "0.0166666667",
        "GX_FRAMES": "20",
        "GX_DEBUG_ASSERT_TEST": "5",
    }, clean=True)
    with audit.settings_preserved():
        result = audit.run_scene("--gfx", environment, width=None, height=None,
            fps=None, frames=20, timeout=120, check=True)
    output = audit.strip_ansi(result.stdout or "")
    if "GX_ASSERT: 1 == 2 at " not in output:
        raise RuntimeError("assert message missing")
    if "name=assert" not in output:
        raise RuntimeError("assert error slot missing")
    fatal = audit.scene_env({
        "GX_FIXED_DT": "0.0166666667",
        "GX_FRAMES": "20",
        "GX_DEBUG_ASSERT_TEST": "5",
        "GX_ASSERT_FATAL": "1",
    }, clean=True)
    with audit.settings_preserved():
        result = subprocess.run([str(BIN), "--gfx"], cwd=ROOT, env=fatal,
            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            timeout=120)
    if result.returncode != SIGABRT_EXIT:
        raise RuntimeError(
            f"fatal assert exit {result.returncode} != {SIGABRT_EXIT}")
    fatal_output = audit.strip_ansi(result.stdout or "")
    if "Caught signal 6" not in fatal_output:
        raise RuntimeError("fatal assert produced no crash dump")
    if "LOG TAIL" not in fatal_output:
        raise RuntimeError("fatal assert crash lost the log tail")
    return True


def log_leg():
    #opencode: GX_LOG_FILE mirrors every line (no ANSI escapes) and
    #   GX_LOG_DEDUP collapses identical repeats; the repeat hook emits five
    #   identical lines that must become one line plus a "repeated" note.
    with tempfile.TemporaryDirectory(prefix="log_check_",
                                     dir="/tmp/opencode") as temporary:
        log_path = Path(temporary) / "run.log"
        environment = audit.scene_env({
            "GX_FIXED_DT": "0.0166666667",
            "GX_FRAMES": "30",
            "GX_LOG_FILE": str(log_path),
            "GX_LOG_DEDUP": "1",
            "GX_LOG_TEST_REPEAT": "5",
        }, clean=True)
        with audit.settings_preserved():
            audit.run_scene("--gfx", environment, width=None, height=None,
                fps=None, frames=30, timeout=120, check=True)
        if not log_path.is_file():
            raise RuntimeError("log file missing")
        text = log_path.read_text()
        if "\x1b[" in text:
            raise RuntimeError("log file contains ANSI escapes")
        if "ENV:" not in text:
            raise RuntimeError("log file missing the run's first lines")
        if len(text.splitlines()) < 50:
            raise RuntimeError("log file transcript too short")
        if "GX_LOG: last message repeated" not in text:
            raise RuntimeError("dedup did not collapse the repeats")
        if text.count("GX_LOG_TEST: repeat") != 1:
            raise RuntimeError("dedup left raw repeats in the file")
    #opencode: per-module levels — gfx=error silences every [GFX] line
    filtered = audit.scene_env({
        "GX_FIXED_DT": "0.0166666667",
        "GX_FRAMES": "20",
        "GX_LOG_MODULE_LEVELS": "gfx=error",
    }, clean=True)
    with audit.settings_preserved():
        result = audit.run_scene("--gfx", filtered, width=None, height=None,
            fps=None, frames=20, timeout=120, check=True)
    if "[GFX]" in audit.strip_ansi(result.stdout or ""):
        raise RuntimeError("GX_LOG_MODULE_LEVELS did not silence GFX")
    #opencode: trace sub-levels — TRACE2 detail hidden at trace (5), shown at
    #   trace2 (4); they used to be mapped to TRACE before filtering
    counts = {}
    for level in ("5", "4"):
        trace_env = audit.scene_env({
            "GX_FIXED_DT": "0.0166666667",
            "GX_FRAMES": "20",
        }, clean=True)
        with audit.settings_preserved():
            result = subprocess.run(
                [str(BIN), "--gfx", "--logLevel", level], cwd=ROOT,
                env=trace_env, text=True, stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT, timeout=120)
        counts[level] = audit.strip_ansi(result.stdout or "").count("DRAW: model")
    if counts["5"] != 0 or counts["4"] == 0:
        raise RuntimeError(f"trace levels not separated: {counts}")
    #opencode: JSON mode — every emitted line parses with the four keys
    with tempfile.TemporaryDirectory(prefix="log_json_",
                                     dir="/tmp/opencode") as temporary:
        json_path = Path(temporary) / "run.jsonl"
        json_env = audit.scene_env({
            "GX_FIXED_DT": "0.0166666667",
            "GX_FRAMES": "20",
            "GX_LOG_JSON": "1",
            "GX_LOG_FILE": str(json_path),
        }, clean=True)
        with audit.settings_preserved():
            audit.run_scene("--gfx", json_env, width=None, height=None,
                fps=None, frames=20, timeout=120, check=True)
        lines = json_path.read_text().splitlines()
        parsed = 0
        for line in lines:
            try:
                doc = json.loads(line)
            except ValueError:
                continue
            if set(doc) != {"ts", "level", "module", "msg"}:
                raise RuntimeError(f"JSON keys wrong: {sorted(doc)}")
            parsed += 1
        if parsed < 100 or parsed < len(lines) * 0.9:
            raise RuntimeError(f"JSON lines: {parsed}/{len(lines)} parsed")
    return True


def mem_leg():
    #opencode: GX_MEM_DEBUG=1 must dump the allocator accounting at exit:
    #   the totals balance (allocs - frees == live) and the peaks dominate.
    environment = audit.scene_env({
        "GX_FIXED_DT": "0.0166666667",
        "GX_FRAMES": "30",
        "GX_MEM_DEBUG": "1",
    }, clean=True)
    with audit.settings_preserved():
        result = audit.run_scene("--gfx", environment, width=None,
            height=None, fps=None, frames=30, timeout=120, check=True)
    output = audit.strip_ansi(result.stdout or "")
    summary = re.search(
        r"GX_MEM: live=(\d+) bytes=(\d+) peak=(\d+) peak_bytes=(\d+) "
        r"allocs=(\d+) frees=(\d+) leaks=(\d+) leak_bytes=(\d+)", output)
    if not summary:
        raise RuntimeError("memory summary missing")
    live, live_bytes, peak, peak_bytes, allocs, frees, leaks, leak_bytes = (
        int(value) for value in summary.groups())
    if allocs < 50 or frees < 40:
        raise RuntimeError(f"too few allocations tracked: {allocs}/{frees}")
    if allocs - frees != live:
        raise RuntimeError(
            f"accounting does not balance: {allocs}-{frees} != {live}")
    if peak < live or peak_bytes < live_bytes:
        raise RuntimeError(
            f"peaks below live: {peak}/{peak_bytes} < {live}/{live_bytes}")
    if peak_bytes < 1000000:
        raise RuntimeError(f"peak bytes implausibly low: {peak_bytes}")
    if leaks > 64:
        raise RuntimeError(f"too many live allocations at exit: {leaks}")
    #opencode: leak entries must carry an allocation site (symbol+offset for
    #   exported callers, base-relative +0x… for static ones)
    if "GX_MEM: leak" in output and not re.search(
            r"site=(\+0x[0-9a-f]+|[A-Za-z_][\w:]*\+0x[0-9a-f]+)", output):
        raise RuntimeError("leak entries carry no allocation site")
    return live, live_bytes, leaks, leak_bytes


def watch_leg():
    environment = audit.scene_env({
        "GX_FIXED_DT": "0.0166666667",
        "GX_FRAMES": "40",
        "GX_DEBUG_WATCH_TEST": "1",
    }, clean=True)
    with audit.settings_preserved():
        result = audit.run_scene("--gfx", environment, width=None,
            height=None, fps=None, frames=40, timeout=120, check=True)
    output = audit.strip_ansi(result.stdout or "")
    if "GX_WATCH: frameCount registered" not in output:
        raise RuntimeError("watch registration missing")
    values = [int(value) for value in WATCH_RE.findall(output)]
    if len(values) < 3:
        raise RuntimeError(f"too few watch samples: {values}")
    if values != sorted(values) or len(set(values)) != len(values):
        raise RuntimeError(f"watch values not strictly increasing: {values}")
    #opencode: threshold variant — a "> N" condition logs exactly one rising
    #   edge (not every change) and the crossing value satisfies the bound.
    threshold_env = audit.scene_env({
        "GX_FIXED_DT": "0.0166666667",
        "GX_FRAMES": "40",
        "GX_DEBUG_WATCH_TEST": "1",
        "GX_DEBUG_WATCH_TEST_THRESHOLD": "20",
    }, clean=True)
    with audit.settings_preserved():
        result = audit.run_scene("--gfx", threshold_env, width=None,
            height=None, fps=None, frames=40, timeout=120, check=True)
    threshold_output = audit.strip_ansi(result.stdout or "")
    crossed = re.findall(r"GX_WATCH: frameCount=(\d+) crossed >20",
        threshold_output)
    if len(crossed) != 1:
        raise RuntimeError(f"threshold watch lines: {crossed}")
    if int(crossed[0]) <= 20:
        raise RuntimeError(f"threshold crossing value {crossed[0]} <= 20")
    return len(values)


def backtrace_leg():
    environment = audit.scene_env({
        "GX_FIXED_DT": "0.0166666667",
        "GX_FRAMES": "20",
        "GX_DEBUG_BACKTRACE": "10",
    }, clean=True)
    with audit.settings_preserved():
        result = audit.run_scene("--gfx", environment, width=None,
            height=None, fps=None, frames=20, timeout=120, check=True)
    output = audit.strip_ansi(result.stdout or "")
    if "STACK (" not in output:
        raise RuntimeError("on-demand backtrace missing")
    stack_text = output.split("STACK (", 1)[-1]
    if "gX_" not in stack_text:
        raise RuntimeError("on-demand backtrace has no symbols")
    return True


def main():
    if not BIN.is_file():
        print("DEBUG_CHECK: missing binary", file=sys.stderr)
        return 1
    try:
        frames = crash_leg()
        samples = watch_leg()
        backtrace_leg()
        live, live_bytes, leaks, leak_bytes = mem_leg()
        assert_leg()
        watchdog_leg()
        log_leg()
    except (OSError, RuntimeError, ValueError) as error:
        print(f"DEBUG_CHECK: FAIL — {error}", file=sys.stderr)
        return 1
    print(f"DEBUG_CHECK: PASS crash trace ({frames} frames, registers, "
          f"symbols, recipe) + variable trace ({samples} increasing samples) "
          f"+ on-demand backtrace + allocator accounting (live {live}/"
          f"{live_bytes} B, leaks {leaks}/{leak_bytes} B) + assert "
          f"macro (message, error slot, fatal abort) + frame watchdog "
          f"(overrun, zone dump, abort) + log system (file sink, dedup, "
          f"module levels, trace sub-levels, JSON)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
#opencode: profiler gate. Runs a gfx scene with GX_PROFILE_DUMP and asserts
#   the dump: a frame total, the main-loop phases and nested zones with calls
#   and sane times, and a phase sum that stays near the frame budget.

import json
import re
import sys
import tempfile
from pathlib import Path

import audit_common as audit


ROOT = audit.ROOT
BIN = audit.BIN
FRAME_RE = re.compile(
    r"GX_PROFILE: frame=(\d+) total_ms=([\d.]+) min_ms=([\d.]+) "
    r"avg_ms=([\d.]+) max_ms=([\d.]+) history=(\d+) entries=(\d+)")
ENTRY_RE = re.compile(
    r"GX_PROFILE:\s+name=([\w:]+) calls=(\d+) ms=([\d.]+) pct=([\d.]+)")
PHASES = ("renderer", "gui", "input", "settings")


def main():
    if not BIN.is_file():
        print("PROFILE_CHECK: missing binary", file=sys.stderr)
        return 1
    try:
        environment = audit.scene_env({
            "GX_FIXED_DT": "0.0166666667",
            "GX_FRAMES": "90",
            "GX_PROFILE_DUMP": "60",
        }, clean=True)
        with audit.settings_preserved():
            result = audit.run_scene("--gfx", environment, width=None,
                height=None, fps=None, frames=90, timeout=120, check=True)
        log = audit.strip_ansi(result.stdout or "")
        frames = FRAME_RE.findall(log)
        if not frames:
            raise RuntimeError("no profiler dump")
        frame, total_ms, min_ms, avg_ms, max_ms, history, entries = frames[-1]
        total_ms = float(total_ms)
        min_ms = float(min_ms)
        avg_ms = float(avg_ms)
        max_ms = float(max_ms)
        history = int(history)
        entries = int(entries)
        if int(frame) < 60:
            raise RuntimeError(f"unexpected dump frame {frame}")
        if entries < 20:
            raise RuntimeError(f"too few entries: {entries}")
        if total_ms <= 0.0 or total_ms > 500.0:
            raise RuntimeError(f"implausible frame total {total_ms} ms")
        if history < 30:
            raise RuntimeError(f"frame history too short: {history}")
        if not (min_ms <= avg_ms <= max_ms):
            raise RuntimeError(
                f"history stats out of order: {min_ms}/{avg_ms}/{max_ms}")
        rows = ENTRY_RE.findall(log)
        if not rows:
            raise RuntimeError("no per-entry lines")
        names = {row[0] for row in rows}
        for phase in PHASES:
            if phase not in names:
                raise RuntimeError(f"phase '{phase}' missing from dump")
        zones = [name for name in names if ":" in name]
        if len(zones) < 3:
            raise RuntimeError(f"too few zones: {zones}")
        phase_sum = 0.0
        for name, calls, ms, pct in rows:
            if int(calls) < 1:
                raise RuntimeError(f"{name}: calls={calls}")
            if float(ms) < 0.0 or float(pct) > 200.0:
                raise RuntimeError(f"{name}: bad ms/pct {ms}/{pct}")
            if ":" not in name:
                phase_sum += float(ms)
        if phase_sum > total_ms * 1.5:
            raise RuntimeError(
                f"phase sum {phase_sum:.2f} ms exceeds 1.5x the frame "
                f"({total_ms:.2f} ms)")
        #opencode: GX_PROFILE_TRACE leg — the Chrome trace export records zone
        #   instances into a heap ring and writes parseable JSON at shutdown.
        with tempfile.TemporaryDirectory(prefix="profile_trace_",
                                         dir="/tmp/opencode") as temporary:
            trace_path = Path(temporary) / "trace.json"
            environment = audit.scene_env({
                "GX_FIXED_DT": "0.0166666667",
                "GX_FRAMES": "40",
                "GX_PROFILE_TRACE": str(trace_path),
            }, clean=True)
            with audit.settings_preserved():
                audit.run_scene("--gfx", environment, width=None, height=None,
                    fps=None, frames=40, timeout=120, check=True)
            if not trace_path.is_file():
                raise RuntimeError("trace export produced no file")
            document = json.loads(trace_path.read_text())
            trace_events = document.get("traceEvents", [])
            if len(trace_events) < 50:
                raise RuntimeError(f"too few trace events: {len(trace_events)}")
            trace_names = {event.get("name", "") for event in trace_events}
            for zone in ("renderer:modules_update", "gui:canvas_updates"):
                if zone not in trace_names:
                    raise RuntimeError(f"trace misses zone '{zone}'")
            for event in trace_events:
                if not isinstance(event.get("ts"), (int, float)) \
                        or not isinstance(event.get("dur"), (int, float)):
                    raise RuntimeError("trace event lacks ts/dur")
                # trace times are microseconds: a zone longer than 60 s or a
                # timestamp past 600 s means the recorder is broken
                if event["dur"] < 0.0 or event["dur"] > 60.0e6:
                    raise RuntimeError(f"implausible trace dur {event['dur']}")
                if event["ts"] < 0.0 or event["ts"] > 600.0e6:
                    raise RuntimeError(f"implausible trace ts {event['ts']}")
    except (OSError, RuntimeError, ValueError) as error:
        print(f"PROFILE_CHECK: FAIL — {error}", file=sys.stderr)
        return 1
    print(f"PROFILE_CHECK: PASS frame {total_ms:.2f} ms (history "
          f"{min_ms:.2f}/{avg_ms:.2f}/{max_ms:.2f}), {entries} entries, "
          f"{len(zones)} zones, phases {phase_sum:.2f} ms, "
          f"{len(trace_events)} trace events")
    return 0


if __name__ == "__main__":
    sys.exit(main())

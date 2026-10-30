#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#opencode: GL-level request/readout gate. With GX_GL_LEVEL=330 the editor must
#   report user=330 with compute/SSBO disabled; with 430 it must enable both.
#   The detected hardware level must allow the request. The persisted
#   settings.toolGlLevel path feeds the same request (0 = auto here, since
#   gates must never write the protected settings file).

import re
import sys
from pathlib import Path

import audit_common as audit


ROOT = audit.ROOT
BIN = audit.BIN
PRESET = ROOT / "resources" / "tools" / "gfx_editor.json"
TRACE_RE = re.compile(
    r"GFX_GL: detected=(\d+) user=(\d+) compute=(\d+) ssbo=(\d+) setting=(\d+)")


def run_readout(level, gl43=None):
    overrides = {
        "GX_FIXED_DT": "0.0166666667",
        "GX_FRAMES": "60",
        "GX_GFX_PRESET_LOAD": str(PRESET),
        "GX_GL_UI_TRACE": "1",
        "GX_GL_LEVEL": str(level),
    }
    #opencode: the default context is GL 3.3; a 4.3 request only lands when the
    #   startup patch env asks for a 4.3 core context.
    if gl43:
        overrides["GX_GL43_CONTEXT"] = gl43
    environment = audit.scene_env(overrides, clean=True)
    result = audit.run_scene("--gfx", environment, width=None, height=None,
                             fps=None, timeout=120, check=True,
                             extra_args=("--gfx-editor",))
    log = audit.strip_ansi(result.stdout or "")
    rows = TRACE_RE.findall(log)
    if not rows:
        raise RuntimeError(f"GL readout trace missing for level {level}")
    return tuple(int(value) for value in rows[-1])


def main():
    if not BIN.is_file() or not PRESET.is_file():
        print("GFX_GL_LEVEL_UI_CHECK: missing binary or preset", file=sys.stderr)
        return 1
    try:
        with audit.settings_preserved():
            for level, expected_compute, gl43 in ((330, 0, None), (430, 1, "1")):
                detected, user, compute, ssbo, setting = run_readout(level, gl43)
                if user != level:
                    raise RuntimeError(
                        f"level {level}: readout reports user={user}")
                if detected < level:
                    raise RuntimeError(
                        f"level {level}: hardware reports detected={detected}")
                if compute != expected_compute or ssbo != expected_compute:
                    raise RuntimeError(
                        f"level {level}: compute={compute} ssbo={ssbo}, "
                        f"expected {expected_compute}")
                if setting != 0:
                    raise RuntimeError(
                        f"level {level}: gate wrote the persisted setting "
                        f"({setting})")
    except (OSError, RuntimeError, ValueError) as error:
        print(f"GFX_GL_LEVEL_UI_CHECK: FAIL — {error}", file=sys.stderr)
        return 1
    print("GFX_GL_LEVEL_UI_CHECK: PASS request 330 disables compute/SSBO, "
          "430 enables them, readout follows")
    return 0


if __name__ == "__main__":
    sys.exit(main())

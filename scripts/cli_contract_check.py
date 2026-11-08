#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: public CLI contract — help is success, unknown/missing options fail
#   closed, and the fresh-install rule is discoverable in help and the
#   getting-started page. Display-free; the failures happen before boot.

import os
import subprocess
import sys

import audit_common as audit


TIMEOUT = 120
DEBUG_LINES = ("parsing arguments...", "args: case", "-> option", "done.")


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def run(args):
    environment = dict(os.environ)
    environment.update({
        "GX_NO_SETTINGS_SAVE": "1",
        "GX_SKIP_TESTS": "1",
        "GX_AUDIT_WINDOW": "hidden",
    })
    return subprocess.run([str(audit.BIN), *args], cwd=audit.ROOT,
        env=environment, text=True, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, timeout=TIMEOUT)


def main():
    if not audit.BIN.is_file():
        print("CLI_CONTRACT: missing binary", file=sys.stderr)
        return 1
    try:
        help_run = run(["--help"])
        require(help_run.returncode == 0,
            f"--help exited {help_run.returncode}, expected 0")
        help_text = audit.strip_ansi(help_run.stdout or "")
        require("Usage:" in help_text, "--help did not print usage")
        require("--title" in help_text and "--gfx" in help_text,
            "--help did not list the front-door scene flags")
        require("resources/settings.json" in help_text,
            "--help did not explain the fresh-install rule")
        require("--logLevel" in help_text and "warning" in help_text
            and "--log-level" in help_text,
            "--help did not document named warning log levels")
        for line in DEBUG_LINES:
            require(line not in help_text,
                f"--help leaked parser debug line: {line!r}")

        bad_run = run(["--definitely-not-a-flag"])
        bad_text = audit.strip_ansi(bad_run.stdout or "")
        require(bad_run.returncode != 0,
            "unknown option exited 0; a typo must fail closed")
        require("unknown option" in bad_text and "run --help" in bad_text,
            f"unknown option message was not actionable: {bad_text[-300:]!r}")
        require("gX_Scene: switched" not in bad_text,
            "unknown option still booted the engine")

        missing_run = run(["--screenWidth"])
        missing_text = audit.strip_ansi(missing_run.stdout or "")
        require(missing_run.returncode != 0,
            "missing option argument exited 0")
        require("missing argument" in missing_text
            or "unknown option" in missing_text,
            f"missing option argument message was unclear: {missing_text[-300:]!r}")

        bad_level = run(["--logLevel", "banana"])
        bad_level_text = audit.strip_ansi(bad_level.stdout or "")
        require(bad_level.returncode != 0,
            "invalid --logLevel exited 0")
        require("--logLevel" in bad_level_text
            and "warning" in bad_level_text,
            f"invalid --logLevel message was not actionable: "
            f"{bad_level_text[-300:]!r}")
        require("gX_Scene: switched" not in bad_level_text,
            "invalid --logLevel still booted the engine")

        alias_help = run(["--log-level", "warning", "--help"])
        require(alias_help.returncode == 0,
            "--log-level alias did not accept a named level")
        numeric_help = run(["--logLevel", "9", "--help"])
        require(numeric_help.returncode == 0,
            "--logLevel no longer accepts numeric enum values")

        warning_run = run(["--logLevel", "warning", "--test-only"])
        warning_text = audit.strip_ansi(warning_run.stdout or "")
        require(warning_run.returncode == 0,
            f"warning-level test-only run exited {warning_run.returncode}")
        for level in ("[ TRACE", "[TRACE2", "[ DEBUG", "[ INFO ",
                      "[NOTICE", "[GREEN ", "[MAGENTA"):
            require(level not in warning_text,
                f"warning level leaked {level!r} output")
        require("[ WARN " in warning_text or "[ERROR!" in warning_text,
            "warning level suppressed its own warning/error output")

        page = audit.ROOT / "docs" / "getting_started.md"
        require(page.is_file(), "docs/getting_started.md is missing")
        page_text = page.read_text(encoding="utf-8")
        require("fresh tree" in page_text and "--title" in page_text,
            "getting-started page does not explain the first-run choice")
    except (OSError, subprocess.SubprocessError, RuntimeError) as error:
        print(f"CLI_CONTRACT: FAIL — {error}", file=sys.stderr)
        return 1
    print("CLI_CONTRACT: PASS help exits 0, unknown/missing "
        "options fail closed, named warning log level filters "
        "lower levels, first-run rule documented")
    return 0


if __name__ == "__main__":
    sys.exit(main())

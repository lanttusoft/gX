#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: disposable-source controls for the logging-level guard.
from pathlib import Path
import tempfile
import unittest

import log_level_audit


HEADER = """\
#define gX_TRACELOG_PROLOGUE(funname,logpart) \\
    gX_TRACELOG_PROLOGUE_LEVEL(gX_LOG_TRACE,funname,logpart)
#define gX_TRACELOG_EPILOGUE(logpart) \\
    gX_TRACELOG_EPILOGUE_LEVEL(gX_LOG_TRACE,logpart)
"""


class LogLevelAuditTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="gx_log_level_audit_")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        (self.root / "src/common").mkdir(parents=True)
        (self.root / "src/common/log.h").write_text(HEADER)
        (self.root / "src/main.c").write_text(
            "void apply(void) { gX_LogState.globalLevel = 9; }\n")

    def test_clean_tree_and_allowed_threshold_site(self):
        (self.root / "src/gui").mkdir()
        (self.root / "src/gui/clean.c").write_text(
            'gX_TRACELOG_PROLOGUE_MODULE(GUI, gX_LOG_TRACE, "x", "message")\n')
        self.assertEqual(log_level_audit.audit(self.root), [])

    def test_threshold_as_severity_is_rejected(self):
        (self.root / "src/gui").mkdir()
        bad = self.root / "src/gui/bad.c"
        bad.write_text(
            'gX_TRACELOG_PROLOGUE_MODULE(GUI, gX_TraceLog_logtypeLevel, "x", "message")\n')
        problems = log_level_audit.audit(self.root)
        self.assertEqual(len(problems), 1)
        self.assertEqual(problems[0]["path"], "src/gui/bad.c")
        self.assertEqual(problems[0]["line"], 1)

    def test_macro_regression_is_rejected(self):
        (self.root / "src/common/log.h").write_text(
            "#define gX_TRACELOG_PROLOGUE(funname,logpart) \\\n"
            "    gX_TRACELOG_PROLOGUE_LEVEL(gX_LogState.globalLevel,funname,logpart)\n"
            "#define gX_TRACELOG_EPILOGUE(logpart) \\\n"
            "    gX_TRACELOG_EPILOGUE_LEVEL(gX_LOG_TRACE,logpart)\n")
        problems = log_level_audit.audit(self.root)
        self.assertTrue(any("PROLOGUE" in item["reason"] for item in problems))


if __name__ == "__main__":
    unittest.main()

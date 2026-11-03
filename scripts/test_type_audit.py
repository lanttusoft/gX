#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: native type-contract fixtures: the AST checks must catch bool sentinels,
#   counters, returns and comparisons, and must not re-report compiler casts.
import tempfile
import unittest
from pathlib import Path

import type_audit


FIXTURE = r"""
typedef _Bool gX_I1;
typedef int gX_I32;
typedef void gX_I0;

static gX_I1 trace = -1;
static gX_I1 flag = 2;
static gX_I1 query(void) { return -1; }

int main(void) {
    gX_I1 count = 0;
    count++;
    if (trace == -1) return 1;
    gX_I1 casted = (gX_I1)-1;
    gX_I32 narrowed = (gX_I32)(gX_I0 *)&count;
    gX_I0 *back = (gX_I0 *)(gX_I32)&count;
    (void)narrowed; (void)back; (void)casted;
    return flag;
}
"""


class TypeAudit(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        try:
            import clang.cindex  # noqa: F401
        except ImportError:
            raise unittest.SkipTest("python clang bindings unavailable")

    def findings(self):
        with tempfile.TemporaryDirectory(prefix="gx_type_audit_") as directory:
            path = Path(directory) / "fixture.c"
            path.write_text(FIXTURE)
            index = type_audit.ci.Index.create()
            tu = index.parse(str(path), args=["-std=gnu11"])
            findings = []
            type_audit.collect(tu, findings)
            return findings

    def test_bool_sentinels_counters_returns_and_comparisons(self):
        messages = [item["message"] for item in self.findings()]
        self.assertTrue(any("sentinel" in message for message in messages))
        self.assertTrue(any("counter" in message for message in messages))
        self.assertTrue(any("returns" in message for message in messages))
        self.assertTrue(any("compared" in message for message in messages))
        self.assertTrue(any("cast of" in message for message in messages))

    def test_compiler_cast_classes_are_not_duplicated(self):
        messages = [item["message"] for item in self.findings()]
        self.assertFalse(any("pointer" in message.lower() for message in messages))


if __name__ == "__main__":
    unittest.main()

#!/usr/bin/env python3
#ai: focused regressions for the declared-vs-defined public API audit (AUD-03).
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import api_definition_check as check


class DeclarationTests(unittest.TestCase):
    def write_header(self, root, relative, text):
        path = Path(root) / "src" / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")

    def test_public_declarations_only(self):
        with tempfile.TemporaryDirectory() as directory:
            self.write_header(directory, "api.h", """
                typedef gX_I0 (*gX_Callback)(gX_I32);
                #define gX_Macro(x) (x)
                gX_I0 gX_Declared(gX_I32 value);
                static inline gX_I0 gX_Inline(void) { return; }
                gX_I0 gX_Commented(void); // gX_Noise(
                /* gX_BlockComment(void); */
                #if 0
                gX_I0 gX_Disabled(void);
                #endif
                #if GX_GUI_USE_POOLS
                gX_I0 gX_PoolOnly(void);
                #endif
                gX_I32 gX_Scripting_ProfileOnly(void);
            """)
            declarations = check.collect_declarations(Path(directory) / "src")
            self.assertIn("gX_Declared", declarations)
            self.assertNotIn("gX_Inline", declarations)
            self.assertNotIn("gX_Noise", declarations)
            self.assertNotIn("gX_BlockComment", declarations)
            self.assertNotIn("gX_Disabled", declarations)
            self.assertNotIn("gX_PoolOnly", declarations)
            self.assertNotIn("gX_Scripting_ProfileOnly", declarations)

    def test_backend_tree_is_skipped(self):
        with tempfile.TemporaryDirectory() as directory:
            self.write_header(directory, "backend/private.h",
                              "gX_I0 gX_BackendOnly(void);\n")
            declarations = check.collect_declarations(Path(directory) / "src")
            self.assertNotIn("gX_BackendOnly", declarations)


class SymbolTests(unittest.TestCase):
    def test_nm_parser_accepts_only_global_function_definitions(self):
        definitions = check.parse_nm_output(
            "gX_Defined T 0000000000000000 0000000000000010\n"
            "gX_Weak W 0000000000000000 0000000000000010\n"
            "gX_Local t 0000000000000000 0000000000000010\n"
            "gX_Undefined U\n")
        self.assertEqual(definitions, {"gX_Defined", "gX_Weak"})

    def test_baseline_is_sorted_and_unique(self):
        baseline = sorted(check.KNOWN_MISSING)
        self.assertEqual(len(baseline), len(set(baseline)))
        self.assertGreater(len(baseline), 0)

    def test_recommendation_classes(self):
        self.assertEqual(check.recommendation("gX_Mesh_GenPoly"), "implement")
        self.assertEqual(check.recommendation("gX_RendererBackground_LoadClear"),
                         "prune")
        self.assertEqual(check.recommendation("gX_Unknown_Thing"), "review")


class EndToEndTests(unittest.TestCase):
    def run_check(self, *arguments):
        return subprocess.run(
            [sys.executable, str(check.ROOT / "scripts" /
                                 "api_definition_check.py"), *arguments],
            cwd=check.ROOT, capture_output=True, text=True, timeout=120)

    def test_current_baseline_passes_and_strict_reports_known_list(self):
        baseline = self.run_check()
        self.assertEqual(baseline.returncode, 0, baseline.stdout + baseline.stderr)
        self.assertIn("0 new", baseline.stdout)
        strict = self.run_check("--strict")
        self.assertEqual(strict.returncode, 1)
        self.assertIn("STRICT FAIL", strict.stdout)

    def test_fake_declaration_is_new_drift(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "src"
            source.mkdir()
            (source / "fake.h").write_text(
                "gX_I0 gX_FakeMissingDeclaration(void);\n", encoding="utf-8")
            result = self.run_check("--src", str(source))
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            self.assertIn("NEW  gX_FakeMissingDeclaration", result.stdout)


if __name__ == "__main__":
    unittest.main()

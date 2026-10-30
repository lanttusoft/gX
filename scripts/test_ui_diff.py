#!/usr/bin/env python3
#ai: 2026-10-22 unit tests for the textual layout-dump diff.
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "scripts" / "ui_diff.py"


def dump(width=120, height=80, elements=None):
    return {
        "screenW": width,
        "screenH": height,
        "canvases": [{
            "name": "TEST",
            "elements": elements if elements is not None else [
                {"name": "A", "type": "LABEL", "x": 10.0, "y": 20.0, "w": 30.0, "h": 8.0},
                {"name": "B", "type": "PANEL", "x": 50.0, "y": 10.0, "w": 40.0, "h": 20.0},
            ],
        }],
    }


class UIDiffTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="gx-ui-diff-")
        self.addCleanup(self.tmp.cleanup)
        self.dir = Path(self.tmp.name)

    def write(self, name, data):
        path = self.dir / name
        path.write_text(json.dumps(data))
        return path

    def run_tool(self, before, after, *extra):
        return subprocess.run(
            [sys.executable, str(TOOL), str(before), str(after), *extra],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    def test_identical_passes(self):
        before = self.write("before.json", dump())
        after = self.write("after.json", dump())
        result = self.run_tool(before, after)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("UI_DIFF: PASS", result.stdout)

    def test_move_within_tolerance_passes(self):
        before = self.write("before.json", dump())
        changed = dump()
        changed["canvases"][0]["elements"][0]["x"] += 1.5
        after = self.write("after.json", changed)
        result = self.run_tool(before, after)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_move_beyond_tolerance_fails(self):
        before = self.write("before.json", dump())
        changed = dump()
        changed["canvases"][0]["elements"][0]["x"] += 3.0
        after = self.write("after.json", changed)
        result = self.run_tool(before, after)
        self.assertEqual(result.returncode, 1)
        self.assertIn("moved", result.stdout)

    def test_resize_beyond_tolerance_fails(self):
        before = self.write("before.json", dump())
        changed = dump()
        changed["canvases"][0]["elements"][1]["w"] += 4.0
        after = self.write("after.json", changed)
        result = self.run_tool(before, after)
        self.assertEqual(result.returncode, 1)
        self.assertIn("resized", result.stdout)

    def test_added_and_removed(self):
        before = self.write("before.json", dump())
        changed = dump(elements=[{"name": "A", "x": 10, "y": 20, "w": 30, "h": 8}])
        after = self.write("after.json", changed)
        result = self.run_tool(before, after)
        self.assertEqual(result.returncode, 1)
        self.assertIn("removed", result.stdout)

        before2 = self.write("before2.json", changed)
        after2 = self.write("after2.json", dump())
        result2 = self.run_tool(before2, after2)
        self.assertEqual(result2.returncode, 1)
        self.assertIn("added", result2.stdout)

    def test_screen_mismatch_fails(self):
        before = self.write("before.json", dump())
        after = self.write("after.json", dump(width=160, height=90))
        result = self.run_tool(before, after)
        self.assertEqual(result.returncode, 1)
        self.assertIn("screen", result.stdout)

    def test_json_report(self):
        before = self.write("before.json", dump())
        changed = dump()
        changed["canvases"][0]["elements"][0]["x"] += 5
        after = self.write("after.json", changed)
        report = self.dir / "report.json"
        result = self.run_tool(before, after, "--json", str(report))
        self.assertEqual(result.returncode, 1)
        data = json.loads(report.read_text())
        self.assertFalse(data["pass"])
        self.assertEqual(data["changes"][0]["kind"], "moved")

    def test_bad_input_exit_2(self):
        bad = self.dir / "bad.json"
        bad.write_text("{ not json")
        good = self.write("good.json", dump())
        result = self.run_tool(bad, good)
        self.assertEqual(result.returncode, 2)
        self.assertIn("UI_DIFF: FAIL", result.stderr)


if __name__ == "__main__":
    unittest.main()

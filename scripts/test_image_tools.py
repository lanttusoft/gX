#!/usr/bin/env python3
#ai: focused synthetic regressions for the generic image review tools.
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np
from PIL import Image

import contact_sheet


ROOT = Path(__file__).resolve().parents[1]


def write_png(path, array):
    Image.fromarray(array.astype(np.uint8)).save(path)


def write_ppm(path, array):
    array = array.astype(np.uint8)
    height, width = array.shape[:2]
    path.write_bytes(
        f"P6\n{width} {height}\n255\n".encode() + array.tobytes())


def run(script, *arguments):
    return subprocess.run(
        [sys.executable, str(ROOT / "scripts" / script), *map(str, arguments)],
        cwd=ROOT, capture_output=True, text=True, timeout=120)


class ImageFixture(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="gx-image-tools-")
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.base = np.zeros((8, 8, 3), dtype=np.uint8)
        self.base[..., 1] = 40
        self.other = self.base.copy()

    def path(self, name):
        return self.directory / name


class SnapshotCompareTests(ImageFixture):
    def compare(self, *arguments):
        return run("snapshot_compare.py", *arguments)

    def make_pair(self):
        first = self.path("first.png")
        second = self.path("second.png")
        write_png(first, self.base)
        write_png(second, self.other)
        return first, second

    def test_identical_png_passes(self):
        first = self.path("same.png")
        write_png(first, self.base)
        result = self.compare(first, first)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("SNAPSHOT_COMPARE: PASS", result.stdout)
        self.assertIn("diff_count=0", result.stdout)

    def test_ppm_and_png_can_be_compared(self):
        ppm = self.path("base.ppm")
        png = self.path("base.png")
        write_ppm(ppm, self.base)
        write_png(png, self.base)
        result = self.compare(ppm, png)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_ppm_crlf_header(self):
        ppm = self.path("crlf.ppm")
        png = self.path("crlf.png")
        ppm.write_bytes(b"P6\r\n8 8\r\n255\r\n" + self.base.tobytes())
        write_png(png, self.base)
        result = self.compare(ppm, png)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_changed_pixel_inside_region_fails(self):
        first, second = self.make_pair()
        self.other[2, 2] = [255, 0, 0]
        write_png(second, self.other)
        result = self.compare(first, second, "--region", "0,0,4,4")
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("SNAPSHOT_COMPARE: FAIL", result.stdout)

    def test_same_change_outside_region_passes(self):
        first, second = self.make_pair()
        self.other[2, 2] = [255, 0, 0]
        write_png(second, self.other)
        result = self.compare(first, second, "--region", "4,4,8,8")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_tolerance_absorbs_small_change(self):
        first, second = self.make_pair()
        self.other[2, 2] = [5, 40, 0]
        write_png(second, self.other)
        loose = self.compare(first, second, "--tolerance", "5")
        strict = self.compare(first, second, "--tolerance", "4")
        self.assertEqual(loose.returncode, 0, loose.stdout + loose.stderr)
        self.assertEqual(strict.returncode, 1, strict.stdout + strict.stderr)

    def test_max_diff_count(self):
        first, second = self.make_pair()
        self.other[0, 0] = [255, 0, 0]
        self.other[0, 1] = [255, 0, 0]
        self.other[0, 2] = [255, 0, 0]
        write_png(second, self.other)
        failed = self.compare(first, second, "--max-diff-count", "2")
        passed = self.compare(first, second, "--max-diff-count", "3")
        self.assertEqual(failed.returncode, 1, failed.stdout + failed.stderr)
        self.assertEqual(passed.returncode, 0, passed.stdout + passed.stderr)

    def test_fraction_bound_replaces_the_default_exact_count(self):
        first, second = self.make_pair()
        self.other[2, 2] = [255, 0, 0]
        write_png(second, self.other)
        fraction = self.compare(first, second, "--max-diff-fraction", "0.02")
        tight = self.compare(first, second, "--max-diff-fraction", "0.01")
        self.assertEqual(fraction.returncode, 0,
                         fraction.stdout + fraction.stderr)
        self.assertEqual(tight.returncode, 1, tight.stdout + tight.stderr)

    def test_json_summary_and_diff_image(self):
        first, second = self.make_pair()
        self.other[2, 2] = [255, 0, 0]
        write_png(second, self.other)
        diff = self.path("diff.png")
        result = self.compare(first, second, "--diff", diff, "--json")
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        summary = json.loads(result.stdout)
        self.assertFalse(summary["pass"])
        self.assertEqual(summary["diff_count"], 1)
        self.assertTrue(diff.is_file())
        with Image.open(diff) as image:
            pixels = np.asarray(image.convert("RGB"))
        self.assertGreater(pixels[2, 2, 0], 200)
        self.assertLess(pixels[0, 0, 0], 100)

    def test_mixed_sizes_and_missing_input_are_usage_errors(self):
        first = self.path("first.png")
        second = self.path("second.png")
        write_png(first, self.base)
        write_png(second, np.zeros((9, 8, 3), dtype=np.uint8))
        mixed = self.compare(first, second)
        missing = self.compare(self.path("absent.png"), second)
        self.assertEqual(mixed.returncode, 2, mixed.stdout + mixed.stderr)
        self.assertIn("image sizes differ", mixed.stderr)
        self.assertEqual(missing.returncode, 2, missing.stdout + missing.stderr)

    def test_plain_ppm_is_rejected(self):
        first = self.path("first.ppm")
        second = self.path("second.ppm")
        first.write_text("P3\n1 1\n255\n0 0 0\n")
        second.write_text("P3\n1 1\n255\n0 0 0\n")
        result = self.compare(first, second)
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn("P6", result.stderr)


class ContactSheetTests(ImageFixture):
    def make_images(self, count=2, size=(16, 12)):
        paths = []
        for index in range(count):
            array = np.zeros((size[1], size[0], 3), dtype=np.uint8)
            array[..., index % 3] = 80 + index * 20
            path = self.path(f"shot_{index}.png")
            write_png(path, array)
            paths.append(path)
        return paths

    def test_sheet_dimensions_and_label_band(self):
        paths = self.make_images()
        out = self.path("sheet.png")
        result = run("contact_sheet.py", *paths, "--out", out,
                     "--columns", "2", "--cell-width", "32")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        with Image.open(out) as image:
            sheet = np.asarray(image.convert("RGB"))
        expected_image_height = round(32 * 12 / 16)
        self.assertEqual(sheet.shape[1], 64)
        self.assertEqual(sheet.shape[0], expected_image_height + contact_sheet.LABEL_HEIGHT)
        self.assertGreater(int(sheet[expected_image_height:, :, :].max()), 100)

    def test_sheet_is_deterministic_and_accepts_ppm(self):
        paths = self.make_images()
        ppm = self.path("shot_ppm.ppm")
        write_ppm(ppm, np.zeros((12, 16, 3), dtype=np.uint8))
        first = self.path("first.png")
        second = self.path("second.png")
        args = ("--columns", "2", "--cell-width", "24")
        self.assertEqual(run("contact_sheet.py", *paths, ppm, "--out", first,
                             *args).returncode, 0)
        self.assertEqual(run("contact_sheet.py", *paths, ppm, "--out", second,
                             *args).returncode, 0)
        self.assertEqual(first.read_bytes(), second.read_bytes())

    def test_dir_mode_sorts_inputs(self):
        self.make_images(3)
        out = self.path("dir_sheet.png")
        result = run("contact_sheet.py", "--dir", self.directory,
                     "--out", out, "--columns", "3", "--cell-width", "16")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        with Image.open(out) as image:
            self.assertEqual(image.width, 48)

    def test_empty_and_mixed_inputs_are_usage_errors(self):
        empty = self.path("empty")
        empty.mkdir()
        empty_result = run("contact_sheet.py", "--dir", empty,
                           "--out", self.path("empty.png"))
        self.assertEqual(empty_result.returncode, 2,
                         empty_result.stdout + empty_result.stderr)
        self.assertIn("no input images", empty_result.stderr)
        first = self.path("a.png")
        second = self.path("b.png")
        write_png(first, self.base)
        write_png(second, np.zeros((9, 8, 3), dtype=np.uint8))
        mixed_result = run("contact_sheet.py", first, second,
                           "--out", self.path("mixed.png"))
        self.assertEqual(mixed_result.returncode, 2,
                         mixed_result.stdout + mixed_result.stderr)
        self.assertIn("mixed image sizes", mixed_result.stderr)


if __name__ == "__main__":
    unittest.main()

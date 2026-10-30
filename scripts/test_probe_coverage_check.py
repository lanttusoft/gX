# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: self-contained controls for the coverage reader and face-ray oracle.
import struct
import tempfile
from pathlib import Path
import unittest

import numpy as np
import probe_coverage_check as c


class CoverageOracle(unittest.TestCase):
    def test_cardinal_faces(self):
        for face in range(6):
            ray = c.face_rays(face, 1, 1)[0, 0]
            expected = np.zeros(3)
            expected[face // 2] = -1 if face % 2 else 1
            np.testing.assert_equal(ray, expected)

    def test_parallel_outside_and_forward_intersection(self):
        rays = c.face_rays(4, 1, 1)
        self.assertTrue(c.box_coverage(np.array([0, 0, 0]), rays, [0, 0, 3], [1, 1, 1])[0, 0])
        self.assertFalse(c.box_coverage(np.array([2, 0, 0]), rays, [0, 0, 3], [1, 1, 1])[0, 0])
        self.assertFalse(c.box_coverage(np.array([0, 0, 0]), rays, [0, 0, -3], [1, 1, 1])[0, 0])

    def test_reader_and_fractional_mip_control(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "face.bin"
            image = np.zeros((2, 2, 4), dtype="<f4")
            header = b"GXCVF32\0" + struct.pack("<IIifff", 2, 2, 4, 0, 0, 0)
            path.write_bytes(header + image.tobytes())
            self.assertEqual(c.evaluate(path)["disagreements"], 0)
            image[0, 0, 3] = 1
            path.write_bytes(header + image.tobytes())
            result = c.evaluate(path)
            self.assertEqual(result["disagreements"], 1)
            self.assertEqual(result["cpu_box_filtered_mip1_fractional_pixels"], 1)
            image[0, 0, 3] = 0.5
            path.write_bytes(header + image.tobytes())
            with self.assertRaisesRegex(ValueError, "nonbinary"):
                c.evaluate(path)
            path.write_bytes(header)
            with self.assertRaisesRegex(ValueError, "payload"):
                c.read_dump(path)


if __name__ == "__main__":
    unittest.main()

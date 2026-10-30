#ai: self-contained controls for distance dump identity and quantization metrics.
from pathlib import Path
import struct
import tempfile
import unittest

import numpy as np
import probe_coverage_check as coverage
import probe_distance_capture_check as distance


class DistanceCaptureReader(unittest.TestCase):
    def fixture(self, path, alpha):
        image = np.zeros((1, 1, 4), dtype="<f4")
        image[0, 0, 3] = alpha
        path.write_bytes(b"GXDVF32\0" + struct.pack("<IIifff", 1, 1, 4, 0, 0, 0)
                         + image.tobytes())

    def test_distance_and_wrong_marker_type(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "face.bin"
            self.fixture(path, 2)
            result = distance.evaluate(path, [0, 0, 3], [1] * 3)
            self.assertEqual(result["local_pixels"], 1)
            self.assertEqual(result["exact_half_matches"], 1)
            self.assertEqual(result["max_radial_world_error"], 0)
            with self.assertRaisesRegex(ValueError, "header"):
                coverage.read_dump(path)

    def test_sky_and_corrupt_distance_controls(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "face.bin"
            self.fixture(path, 0)
            self.assertEqual(distance.evaluate(path)["coverage_disagreements"], 0)
            self.fixture(path, 1)
            self.assertEqual(distance.evaluate(path)["coverage_disagreements"], 1)
            result = distance.evaluate(path, [0, 0, 3], [1] * 3)
            self.assertEqual(result["over_one_half_ulp_differences"], 1)
            self.fixture(path, np.nan)
            with self.assertRaisesRegex(ValueError, "nonfinite"):
                distance.evaluate(path)

    def test_half_precision_step_controls(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "face.bin"
            neighbor = np.nextafter(np.float16(2), np.float16(3), dtype=np.float16)
            self.fixture(path, neighbor)
            result = distance.evaluate(path, [0, 0, 3], [1] * 3)
            self.assertEqual(result["one_half_ulp_differences"], 1)
            self.assertEqual(result["over_one_half_ulp_differences"], 0)
            self.fixture(path, 2.0001)
            result = distance.evaluate(path, [0, 0, 3], [1] * 3)
            self.assertEqual(result["non_half_representable_values"], 1)

    def test_world_distance_source_contract(self):
        root = Path(__file__).resolve().parents[1]
        shader = (root / "resources/shader/standard.frag").read_text()
        self.assertIn("finalColor.a = length(fs_in.fragPosition - viewPosition);", shader)
        draw = (root / "src/renderer/gfx/draw.c").read_text()
        self.assertIn('mode == 2 ? "GXDVF32" : "GXCVF32"', draw)
        self.assertIn("gfx_probe_coverage_uniform(cgfx->shader.shader, 0);", draw)


if __name__ == "__main__":
    unittest.main()

#!/usr/bin/env python3
#ai: synthetic tests for the colour-independence review; no display/GPU.
import sys
import unittest
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import colour_independence_check as colour


def image(value=40):
    return np.full((colour.HEIGHT, colour.WIDTH, 3), value, dtype=np.float32)


class SimulationTests(unittest.TestCase):
    def test_identity_and_shape(self):
        source = image(80)
        for name in colour.SIMULATIONS:
            result = colour.simulate(source, name)
            self.assertEqual(result.shape, source.shape)
            self.assertTrue(np.isfinite(result).all())

    def test_distinct_simulation_changes_pixels(self):
        source = image(80)
        delta = colour.image_delta(
            source, colour.simulate(source, "deuteranopia"))
        self.assertGreater(delta["max_channel"], 0.0)


class CueTests(unittest.TestCase):
    def test_required_cues(self):
        source = 'HP ATB STATUS RECENT IMPACTS YOU AI'
        self.assertEqual(colour.required_cues_present(source), [])
        self.assertEqual(
            colour.required_cues_present("HP only"), ["ATB", "STATUS",
                "RECENT IMPACTS", "YOU", "AI"])

    def test_shape_mismatch(self):
        with self.assertRaises(ValueError):
            colour.image_delta(image(), image()[:100])


if __name__ == "__main__":
    unittest.main()

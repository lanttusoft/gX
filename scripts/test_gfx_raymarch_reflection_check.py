#!/usr/bin/env python3
#ai: reflection oracle must have usable positive support and explicit no-target controls.
import unittest
import json
import tempfile
from pathlib import Path
import numpy as np

from gfx_raymarch_reflection_check import reflection_oracle, sphere_distance, cube_distance, write_report


class ReflectionOracleTests(unittest.TestCase):
    def test_positive_targets_are_available(self):
        for oblique in (False,True):
            hit,miss,_,x,y = reflection_oracle(640,480,"reflect",oblique)
            self.assertGreater(int(hit.sum()),100)
            self.assertFalse(np.any(hit&miss))
            self.assertTrue(np.all((x[hit]>0)&(x[hit]<640)&(y[hit]>0)&(y[hit]<480)))

    def test_absent_target_is_not_a_positive(self):
        for oblique in (False,True):
            hit,miss,*_ = reflection_oracle(640,480,"reflect-empty",oblique)
            self.assertFalse(hit.any())
            self.assertGreater(int(miss.sum()),10000)

    def test_unavailable_depth_fixture_has_no_false_positive_support(self):
        hit,miss,*_ = reflection_oracle(640,480,"front",False)
        self.assertFalse(hit.any())
        self.assertTrue(miss.any())

    def test_known_ray_intersections(self):
        direction = np.array([0.,0.,-1.])
        self.assertEqual(float(cube_distance(np.array([0.,0.,8.]),direction)),7.)
        self.assertAlmostEqual(float(sphere_distance(np.array([0.,0.,8.]),direction,np.zeros(3))),6.7)

    def test_target_back_face_is_not_camera_visible(self):
        origin = np.array([0.,0.,8.])
        center = np.array([-1.8,0.,4.])
        half = np.array([.45,.7,.45])
        for z,visible in ((4.45,True),(3.55,False)):
            target = center+np.array([0.,0.,z-4.])
            view = target-origin
            length = np.linalg.norm(view)
            front = float(cube_distance(origin,view/length,center,half))
            self.assertEqual(abs(front-length)<.02,visible)

    def test_incomplete_report_cannot_claim_pass(self):
        with tempfile.TemporaryDirectory(prefix="reflection_report_",dir="/tmp/opencode") as directory:
            root = Path(directory)
            for complete,failures,passed in ((False,[],False),(True,["miss"],False),(True,[],True)):
                write_report(root,[],failures,complete)
                report = json.loads((root/"reflection_report.json").read_text())
                self.assertEqual(report["fixture_passed"],passed)
                self.assertFalse(report["general_quality_certified"])


if __name__ == "__main__":
    unittest.main()

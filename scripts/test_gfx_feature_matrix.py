#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2025-2026 lanttu, opencode
# This file is part of gX. See LICENSE.

#ai: report reasons must faithfully describe the existing admission rules.
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory
from unittest.mock import patch

import gfx_feature_matrix as matrix


class VerdictExplanationTests(unittest.TestCase):
    def setUp(self):
        self.metrics = {"frac":0.1,"mean0":100,"mean1":110,"black":0.0}
        self.case = {"lo":0.01,"hi":0.2,"dthresh":10}

    def test_positive(self):
        report = matrix.explain_metrics(self.metrics,self.case)
        self.assertEqual(report["failures"],[])
        self.assertEqual(report["evidence"],"visible effect")

    def test_each_failure_has_a_reason(self):
        for key,value,reason in (("frac",0,"too small"),("frac",0.3,"too broad"),
                                  ("black",0.5,"collapse"),("mean0",5,"brightness"),
                                  ("mean1",250,"brightness")):
            with self.subTest(key=key,value=value):
                metrics = dict(self.metrics,**{key:value})
                reasons = matrix.explain_metrics(metrics,self.case)["failures"]
                self.assertTrue(any(reason in item for item in reasons))

    def test_negative_control_boundary(self):
        case = dict(self.case,lo=None,hi=None)
        self.assertTrue(matrix.explain_metrics(dict(self.metrics,frac=0.002),case)["failures"])
        self.assertFalse(matrix.explain_metrics(dict(self.metrics,frac=0.001),case)["failures"])

    def test_zero_floor_does_not_claim_visibility(self):
        report = matrix.explain_metrics(dict(self.metrics,frac=0),dict(self.case,lo=0))
        self.assertEqual(report["failures"],[])
        self.assertIn("zero change permitted",report["evidence"])


#ai: prevent profile leakage and asymmetric fixture controls.
class CaptureContractTests(unittest.TestCase):
    def test_mirror_uses_dedicated_camera_without_changing_other_rows(self):
        base = matrix.fixture_base("mirror")
        self.assertEqual([base[key] for key in ("GX_CAM_YAW", "GX_CAM_PITCH", "GX_CAM_DIST")],
                         ["45", "20", "12"])
        self.assertEqual(matrix.fixture_base("bloom"), matrix.BASE_OFF)

    def test_probes_use_minimal_scene_native_distance(self):
        base = matrix.fixture_base("probes")
        self.assertEqual(base["GX_CAM_DIST"], "6")
        self.assertEqual(base["GX_CAM_YAW"], matrix.BASE_OFF["GX_CAM_YAW"])
        self.assertEqual(base["GX_CAM_PITCH"], matrix.BASE_OFF["GX_CAM_PITCH"])

    def test_probe_consumer_is_present_in_both_arms(self):
        off, custom = matrix.off_environment("probes")
        on = next(row[2] for row in matrix.ROWS if row[0] == "probes")
        self.assertTrue(custom)
        self.assertEqual(off["GX_CUBEMAP_TEST"], on["GX_CUBEMAP_TEST"])
        self.assertEqual(matrix.BASE_OFF["GX_PROBE_GRID"], "0")
        self.assertEqual(on["GX_PROBE_GRID"], "1")

    def test_profile_jobs_are_separated(self):
        jobs = {"off.png": ("--showcase", {}, "off", "gl33"),
                "on.png": ("--showcase", {}, "on", "gl43")}
        with patch.object(matrix, "capture_profile_jobs", return_value={}) as capture:
            matrix.capture_jobs_to_pool(jobs, 1, 120)
        self.assertEqual(capture.call_count, 2)
        for call in capture.call_args_list:
            subset, _, _, binary = call.args
            self.assertEqual(set(subset), {"off.png"} if binary == "gl33" else {"on.png"})

    def test_replay_preserves_selected_binary(self):
        case = {"offEnv": {}, "caseName": "particles", "scene": "--showcase",
                "binary": "/owned/gl43/gX_build"}
        self.assertIn("/owned/gl43/gX_build", matrix.replay_command(case, "off", 120))

    def test_cache_identity_distinguishes_binaries(self):
        with TemporaryDirectory() as directory:
            first, second = (Path(directory) / name for name in ("gl33", "gl43"))
            first.write_text("same")
            second.write_text("same")
            args = ("--showcase", {}, 1024, 768, 60, 120)
            self.assertNotEqual(matrix.audit._fingerprint(*args, binary=first),
                                matrix.audit._fingerprint(*args, binary=second))

    def test_pool_forwards_selected_binary(self):
        with TemporaryDirectory() as directory:
            binary = Path(directory) / "gl43"
            pool = matrix.audit.CapturePool(raw_dir=directory, binary=binary)
            with patch.object(matrix.audit, "run_scene") as run:
                pool._capture("owned", "--showcase", {}, 1024, 768, 60, 120, 0)
            self.assertEqual(run.call_args.kwargs["binary"], binary)


if __name__ == "__main__":
    unittest.main()

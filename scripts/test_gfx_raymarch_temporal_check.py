#!/usr/bin/env python3
#ai: independent camera projection contracts precede GPU temporal tests.
import unittest

import numpy as np

from gfx_raymarch_temporal_check import camera_basis, project, oracle, eligibility_masks, bilinear


class TemporalOracleTests(unittest.TestCase):
    def test_orbit_basis_is_orthonormal(self):
        for frame in (0,20,90):
            origin,forward,right,up = camera_basis(frame,.01)
            axes = np.stack((forward,right,up))
            np.testing.assert_allclose(axes@axes.T,np.eye(3),atol=1e-14)
            self.assertAlmostEqual(float(np.linalg.norm(origin)),8.)

    def test_center_projects_to_center(self):
        for frame in (0,20,90):
            uv,depth = project(np.zeros(3),640,480,frame,.01)
            np.testing.assert_allclose(uv,[.5,.5],atol=1e-14)
            self.assertAlmostEqual(float(depth),100/99.9-10/(99.9*8))

    def test_static_geometry_motion_excludes_jitter(self):
        jitter = np.array([.25/640,-.4/480])
        previous = np.array([-.25/640,.4/480])
        mask,motion,_,history = oracle(640,480,20,0.,jitter,previous)
        self.assertGreater(int(mask.sum()),10000)
        np.testing.assert_allclose(motion[mask],0.,atol=1e-14)
        y,x = np.mgrid[:480,:640]
        uv = np.stack(((x+.5)/640,1-(y+.5)/480),axis=-1)
        np.testing.assert_allclose(history[mask],(uv+(previous-jitter)/2)[mask],atol=1e-14)

    def test_real_camera_motion_is_nonzero(self):
        mask,motion,depth,history = oracle(640,480,20,.005,[0,0],[0,0])
        self.assertGreater(float(np.linalg.norm(motion[mask],axis=-1).mean()),.001)
        self.assertTrue(np.isfinite(history[mask]).all())
        self.assertTrue(np.all((depth[mask]>0)&(depth[mask]<1)))

    def test_disocclusion_oracle_has_strong_support(self):
        stable,disoccluded,_,_ = eligibility_masks(640,480,20,.3,[0,0],[0,0])
        self.assertGreater(int(stable.sum()),1000)
        self.assertGreater(int(disoccluded.sum()),100)
        self.assertFalse(np.any(stable&disoccluded))

    def test_bilinear_uses_bottom_left_uv(self):
        image = np.array([[1.,2.],[3.,4.]])
        self.assertEqual(float(bilinear(image,np.array([.25,.25]))),3.)
        self.assertEqual(float(bilinear(image,np.array([.5,.5]))),2.5)

    def test_occluder_in_footprint_is_not_lost_to_nearest(self):
        image = np.array([[1.,.98]])
        uv = np.array([.375,.5])
        sampled = float(bilinear(image,uv))
        self.assertAlmostEqual(sampled,.995)
        self.assertGreater(abs(sampled-1.),.0017)
        self.assertEqual(float(image[0,int(uv[0]*2)]),1.)


if __name__ == "__main__":
    unittest.main()

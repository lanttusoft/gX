#ai: pin still-camera preservation and moving-camera stale-history rejection.
import unittest

from gtao_orbit_check import CONTACT_CUT, CONTACT_HISTORY_MAX_DELTA, HISTORY_PAD


def history_compatible(camera_moving, history_ao, current_ao):
    delta = abs(history_ao - current_ao)
    motion_compatible = not camera_moving or delta <= HISTORY_PAD
    contact_fresh = (not camera_moving or min(history_ao, current_ao) >= CONTACT_CUT
        or delta <= CONTACT_HISTORY_MAX_DELTA)
    return motion_compatible and contact_fresh


class GtaoOrbitGateTests(unittest.TestCase):
    def test_stationary_camera_preserves_temporal_accumulation(self):
        self.assertTrue(history_compatible(False, 0.2, 0.9))

    def test_moving_camera_rejects_stale_history(self):
        self.assertFalse(history_compatible(True, 0.2, 0.9))

    def test_moving_camera_rejects_stale_contact_history_inside_pad(self):
        self.assertFalse(history_compatible(True, 0.70, 0.72))

    def test_moving_camera_keeps_fresh_contact_history(self):
        self.assertTrue(history_compatible(True, 0.70, 0.702))

    def test_moving_camera_keeps_nearby_noncontact_history(self):
        self.assertTrue(history_compatible(True, 0.99, 1.0))


if __name__ == '__main__':
    unittest.main()

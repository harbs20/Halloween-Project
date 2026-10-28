"""Tests for tracking.py. They need no camera: python3 -m unittest -v"""

import unittest

from tracking import (AxisMapper, AxisSettings, Face, FaceTracker, State,
                      TrackerSettings, gaze_offset_deg, normalize)

W, H = 640, 360


def face_at(cx, cy, size=80):
    return Face(cx - size / 2, cy - size / 2, size, size)


def tracker(**kw):
    kw.setdefault("smoothing_s", 0)
    kw.setdefault("acquire_frames", 1)
    return FaceTracker(TrackerSettings(**kw), W, H)


class Coordinates(unittest.TestCase):
    def test_center_and_corners(self):
        self.assertEqual(normalize(W / 2, H / 2, W, H), (500, 500))
        self.assertEqual(normalize(0, 0, W, H), (0, 0))
        self.assertEqual(normalize(W, H, W, H), (1000, 1000))

    def test_clamped(self):
        self.assertEqual(normalize(-10, H + 10, W, H), (0, 1000))


class Selection(unittest.TestCase):
    def test_starts_with_largest_face(self):
        r = tracker().update([face_at(100, 180, 60), face_at(500, 180, 120)], 0.0)
        self.assertEqual(r.state, State.TRACKING)
        self.assertAlmostEqual(r.x, 500 / W * 1000)

    def test_keeps_target_when_bigger_face_appears(self):
        t = tracker()
        t.update([face_at(100, 180, 60)], 0.0)
        r = t.update([face_at(110, 180, 60), face_at(500, 180, 150)], 0.05)
        self.assertAlmostEqual(r.x, 110 / W * 1000)

    def test_acquire_needs_consecutive_frames(self):
        t = tracker(acquire_frames=3)
        self.assertEqual(t.update([face_at(300, 180)], 0.0).state, State.LOST)
        self.assertEqual(t.update([face_at(305, 180)], 0.03).state, State.LOST)
        self.assertEqual(t.update([face_at(310, 180)], 0.06).state, State.TRACKING)

    def test_ignores_tiny_faces(self):
        self.assertEqual(tracker(min_face_px=30).update([face_at(300, 180, 10)], 0).state, State.LOST)


class Dropouts(unittest.TestCase):
    def test_hold_then_lost(self):
        t = tracker(hold_time_s=0.5)
        t.update([face_at(400, 180)], 0.0)
        held = t.update([], 0.3)
        self.assertEqual(held.state, State.HOLDING)
        self.assertAlmostEqual(held.x, 400 / W * 1000)
        lost = t.update([], 0.6)
        self.assertEqual(lost.state, State.LOST)
        self.assertIsNone(lost.x)

    def test_resumes_same_face_after_dropout(self):
        t = tracker(hold_time_s=0.5)
        t.update([face_at(400, 180)], 0.0)
        t.update([], 0.2)
        r = t.update([face_at(420, 180), face_at(100, 180, 200)], 0.3)
        self.assertEqual(r.state, State.TRACKING)
        self.assertAlmostEqual(r.x, 420 / W * 1000)

    def test_other_face_during_hold_does_not_steal(self):
        t = tracker(hold_time_s=0.5)
        t.update([face_at(400, 180)], 0.0)
        self.assertEqual(t.update([face_at(50, 180)], 0.2).state, State.HOLDING)


class Smoothing(unittest.TestCase):
    def test_moves_part_way(self):
        t = tracker(smoothing_s=0.2)
        t.update([face_at(320, 180)], 0.0)
        r = t.update([face_at(380, 180)], 0.2)  # One time constant: ~63%.
        target = 380 / W * 1000
        self.assertAlmostEqual(r.x, 500 + 0.632 * (target - 500), delta=1)


class Angles(unittest.TestCase):
    def test_gaze_offset(self):
        self.assertAlmostEqual(gaze_offset_deg(500, 66), 0)
        self.assertAlmostEqual(gaze_offset_deg(1000, 66), 33)
        self.assertAlmostEqual(gaze_offset_deg(0, 66), -33)

    def test_absolute_not_incremental(self):
        m = AxisMapper(AxisSettings(min_deg=0, max_deg=180, deadband_deg=0))
        first = m.target(750)
        for _ in range(10):
            self.assertEqual(m.target(750), first)

    def test_limits_reverse_and_lost(self):
        m = AxisMapper(AxisSettings(center_deg=90, min_deg=80, max_deg=100))
        self.assertEqual(m.target(1000), 100)
        self.assertEqual(m.target(0), 80)
        self.assertEqual(m.target(None), 90)
        r = AxisMapper(AxisSettings(reverse=True, min_deg=0, max_deg=180))
        self.assertLess(r.target(800), 90)

    def test_deadband(self):
        m = AxisMapper(AxisSettings(min_deg=0, max_deg=180, deadband_deg=1))
        self.assertEqual(m.target(505), 90)  # ~0.3 degrees: ignored.

    def test_disabled_axis_stays_centered(self):
        self.assertEqual(AxisMapper(AxisSettings(enabled=False)).target(900), 90)


if __name__ == "__main__":
    unittest.main()

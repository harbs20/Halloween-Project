"""Camera-independent tracking logic for The Watcher.

Everything here is plain Python so it can be tested on any computer without a
camera, OpenCV, or a serial port. watcher.py feeds it face boxes and reads back
normalized coordinates, a tracking state, and servo target angles.

Coordinates: X runs 0 (left edge of the camera image) to 1000 (right edge),
Y runs 0 (top) to 1000 (bottom), and 500,500 is the image center. These are
always the camera's own image coordinates, never the mirrored preview.
"""

from __future__ import annotations

import math
from dataclasses import dataclass
from enum import Enum
from typing import Optional, Sequence


class State(Enum):
    TRACKING = "T"  # The selected face was detected in this frame.
    HOLDING = "H"   # The face was missed briefly; keep its last position.
    LOST = "L"      # No target: missing longer than hold time, or never found.


@dataclass
class Face:
    """One detection in pixel coordinates (x, y is the top-left corner)."""
    x: float
    y: float
    w: float
    h: float
    score: float = 1.0

    @property
    def cx(self) -> float:
        return self.x + self.w / 2

    @property
    def cy(self) -> float:
        return self.y + self.h / 2

    @property
    def area(self) -> float:
        return self.w * self.h


def normalize(px: float, py: float, width: int, height: int) -> tuple[float, float]:
    """Convert a pixel position to the 0-1000 coordinate system."""
    nx = px / width * 1000.0
    ny = py / height * 1000.0
    return min(max(nx, 0.0), 1000.0), min(max(ny, 0.0), 1000.0)


@dataclass
class TrackerSettings:
    hold_time_s: float = 0.6       # How long a missing face is HOLDING before LOST.
    smoothing_s: float = 0.15      # Coordinate smoothing time constant; 0 = none.
    match_distance: float = 1.5    # Max jump between frames, in face widths.
    acquire_frames: int = 2        # Frames a new face must appear before tracking.
    min_face_px: float = 20.0      # Ignore faces narrower than this.


@dataclass
class TrackResult:
    state: State
    x: Optional[float]             # Smoothed 0-1000 coordinates, None when LOST.
    y: Optional[float]
    face: Optional[Face]           # Detected box this frame, None unless TRACKING.


class FaceTracker:
    """Picks one face and follows it instead of jumping between people.

    1. With no target, the largest face becomes a candidate. It must be seen in
       acquire_frames consecutive frames before it is tracked, which filters
       one-frame false detections.
    2. While tracking, each frame keeps the detection closest to the target's
       last position, as long as it is within match_distance face widths. A
       bigger face elsewhere does not steal the target.
    3. If the target is missed, the last position is held for hold_time_s and
       only a face near that position can resume it. After that the target is
       LOST and step 1 starts again.
    """

    def __init__(self, settings: TrackerSettings, width: int, height: int):
        self.s = settings
        self.width = width
        self.height = height
        self._target: Optional[Face] = None      # Last matched raw detection.
        self._x: Optional[float] = None          # Smoothed normalized position.
        self._y: Optional[float] = None
        self._last_seen: Optional[float] = None  # Timestamp of last match.
        self._last_update: Optional[float] = None
        self._candidate: Optional[Face] = None
        self._candidate_count = 0

    def reset(self) -> None:
        self.__init__(self.s, self.width, self.height)

    def update(self, faces: Sequence[Face], now: float) -> TrackResult:
        faces = [f for f in faces if f.w >= self.s.min_face_px]

        if self._target is not None:
            match = self._closest(faces, self._target)
            if match is not None:
                self._accept(match, now)
                return TrackResult(State.TRACKING, self._x, self._y, match)
            if now - self._last_seen <= self.s.hold_time_s:
                return TrackResult(State.HOLDING, self._x, self._y, None)
            self._drop_target()

        return self._acquire(faces, now)

    def _acquire(self, faces: Sequence[Face], now: float) -> TrackResult:
        if not faces:
            self._candidate = None
            self._candidate_count = 0
            return TrackResult(State.LOST, None, None, None)

        # Prefer the face that continues the current candidate; otherwise the
        # largest face starts a new candidate.
        nearby = self._closest(faces, self._candidate) if self._candidate else None
        if nearby is not None:
            self._candidate_count += 1
        else:
            nearby = max(faces, key=lambda f: f.area)
            self._candidate_count = 1
        self._candidate = nearby

        if self._candidate_count < self.s.acquire_frames:
            return TrackResult(State.LOST, None, None, None)

        self._candidate = None
        self._candidate_count = 0
        self._x = self._y = None  # Start smoothing fresh at the new face.
        self._accept(nearby, now)
        return TrackResult(State.TRACKING, self._x, self._y, nearby)

    def _closest(self, faces: Sequence[Face], ref: Face) -> Optional[Face]:
        limit = self.s.match_distance * max(ref.w, 1.0)
        best, best_d = None, limit
        for f in faces:
            d = math.hypot(f.cx - ref.cx, f.cy - ref.cy)
            if d <= best_d:
                best, best_d = f, d
        return best

    def _accept(self, face: Face, now: float) -> None:
        nx, ny = normalize(face.cx, face.cy, self.width, self.height)
        if self._x is None or self._last_update is None:
            self._x, self._y = nx, ny
        else:
            a = smoothing_alpha(now - self._last_update, self.s.smoothing_s)
            self._x += a * (nx - self._x)
            self._y += a * (ny - self._y)
        self._target = face
        self._last_seen = now
        self._last_update = now

    def _drop_target(self) -> None:
        self._target = None
        self._x = self._y = None
        self._last_seen = None
        self._last_update = None


def smoothing_alpha(dt: float, time_constant: float) -> float:
    """Exponential smoothing factor that behaves the same at any frame rate.

    After time_constant seconds the output has moved about 63% of the way to a
    new steady value. Larger = smoother but laggier.
    """
    if time_constant <= 0:
        return 1.0
    return 1.0 - math.exp(-max(dt, 0.0) / time_constant)


@dataclass
class AxisSettings:
    """Maps one image axis to one servo. All angles are in degrees."""
    enabled: bool = True
    center_deg: float = 90.0     # Servo angle that points the eye at image center.
    min_deg: float = 80.0        # Conservative limits for first calibration.
    max_deg: float = 100.0
    reverse: bool = False        # Flip if the eye moves the wrong way.
    fov_deg: float = 66.0        # Camera field of view along this axis.
    gain: float = 1.0            # Scales the angle; tune for camera/eye offset.
    deadband_deg: float = 0.5    # Ignore changes smaller than this (servo jitter).


def gaze_offset_deg(coord: float, fov_deg: float) -> float:
    """Angle from the camera's optical axis to a 0-1000 image coordinate.

    Uses the pinhole camera model, so the image edge (0 or 1000) maps to
    exactly half the field of view. Negative = left/up, positive = right/down.
    """
    half = math.radians(fov_deg) / 2
    t = (coord - 500.0) / 500.0
    return math.degrees(math.atan(t * math.tan(half)))


class AxisMapper:
    """Turns a 0-1000 coordinate into an absolute servo target angle.

    The camera is fixed, so each image position corresponds to one gaze
    direction. This is not a feedback loop that nudges the servo until the face
    is centered: the same face position always gives the same angle.
    """

    def __init__(self, settings: AxisSettings):
        self.s = settings
        self.angle = settings.center_deg

    def target(self, coord: Optional[float]) -> float:
        """Angle for a coordinate; None (no target) returns to center."""
        s = self.s
        if not s.enabled or coord is None:
            raw = s.center_deg
        else:
            offset = gaze_offset_deg(coord, s.fov_deg) * s.gain
            raw = s.center_deg + (-offset if s.reverse else offset)
        raw = min(max(raw, s.min_deg), s.max_deg)
        if abs(raw - self.angle) >= s.deadband_deg or raw in (s.min_deg, s.max_deg):
            self.angle = raw
        return self.angle

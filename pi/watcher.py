#!/usr/bin/env python3
"""The Watcher: follow one face with the Pi camera and output eye servo angles.

    python3 watcher.py                          # print angles in the terminal
    python3 watcher.py --preview                # also show the debug window
    python3 watcher.py --list-ports             # find the Arduino's port
    python3 watcher.py --serial /dev/ttyACM0    # also send angles to the Arduino

Settings live in watcher.toml. Serial output is only used when --serial names a
port. The line protocol is documented in PROTOCOL.md.
"""

from __future__ import annotations

import argparse
import sys
import time
import tomllib
from dataclasses import fields
from pathlib import Path

# The "thinking" (picking a face, smoothing, angle math) lives in tracking.py.
# This file connects it to the camera, the face detector, and the Arduino.
from tracking import AxisMapper, AxisSettings, Face, FaceTracker, State, TrackerSettings

# Folder this script is in, so the model and settings are found from anywhere.
HERE = Path(__file__).resolve().parent


def load_config(path: Path) -> dict:
    # Reads watcher.toml into a dictionary, e.g. cfg["pan"]["gain"].
    with open(path, "rb") as f:
        return tomllib.load(f)


def build(cls, table: dict):
    """Create a settings dataclass from a TOML table, rejecting typos."""
    known = {f.name for f in fields(cls)}
    unknown = set(table) - known
    if unknown:
        sys.exit(f"Unknown setting(s) for {cls.__name__}: {', '.join(sorted(unknown))}")
    return cls(**table)


class YuNetDetector:
    def __init__(self, cfg: dict, width: int, height: int):
        import cv2

        if not hasattr(cv2, "FaceDetectorYN"):
            sys.exit(f"OpenCV {cv2.__version__} has no FaceDetectorYN. See README.md.")
        model = HERE / cfg["model"]
        if not model.exists():
            sys.exit(f"Model not found: {model}\nRun ./setup.sh to download it.")
        # YuNet is a small neural network built into OpenCV. It needs the frame
        # size up front, plus how confident it must be to count a face.
        self.net = cv2.FaceDetectorYN.create(
            str(model), "", (width, height),
            cfg["score_threshold"], cfg["nms_threshold"], cfg["top_k"],
        )

    def detect(self, frame) -> list[Face]:
        # Finds every face in one camera frame.
        _, rows = self.net.detect(frame)
        if rows is None:
            return []
        # Each row: x, y, w, h, 10 landmark values (eyes, nose, mouth), score.
        # Only the box (top-left corner, width, height) and score are kept.
        return [Face(float(r[0]), float(r[1]), float(r[2]), float(r[3]), float(r[14]))
                for r in rows]


def open_camera(cfg: dict):
    from libcamera import Transform
    from picamera2 import Picamera2

    picam2 = Picamera2()
    # The full-width sensor mode keeps the whole field of view, so the angle
    # math (fov_deg) is correct. hflip/vflip are for a camera mounted rotated.
    config = picam2.create_preview_configuration(
        main={"size": (cfg["width"], cfg["height"]), "format": "RGB888"},  # BGR order for OpenCV
        sensor={"output_size": (cfg["sensor_width"], cfg["sensor_height"])},
        transform=Transform(hflip=cfg["hflip"], vflip=cfg["vflip"]),
        controls={"FrameRate": cfg["fps"]},
    )
    picam2.configure(config)
    picam2.start()
    return picam2


class SerialLink:
    """Sends one protocol line per send period and echoes Arduino replies."""

    def __init__(self, port: str, cfg: dict):
        import serial

        # timeout=0 means reads never wait, so the camera loop never stalls.
        self.ser = serial.Serial(port, cfg["baud"], timeout=0, write_timeout=0.1)
        self.period = 1.0 / cfg["send_hz"]
        self.echo = cfg["echo"]
        self.next_send = 0.0
        self.rx = b""
        print(f"Opened {port} at {cfg['baud']} baud; waiting for the Arduino to restart...")
        self._wait_ready(cfg["reset_wait_s"])

    def _wait_ready(self, timeout: float) -> None:
        """Opening the port restarts an Uno. Its sketch prints a READY line once
        it is listening; wait for that instead of guessing a delay."""
        deadline = time.monotonic() + timeout
        buf = b""
        while time.monotonic() < deadline:
            buf += self.ser.read(64)
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                text = raw.decode("ascii", "replace").strip()
                if text.startswith("READY"):
                    print(f"[arduino] {text}")
                    return
            time.sleep(0.01)
        print(f"Warning: no READY from the Arduino after {timeout} s. "
              "Is the eye sketch uploaded? Sending anyway.")

    def maybe_send(self, line: str, now: float, force: bool = False) -> None:
        # Sends at a steady rate (send_hz) even if nothing changed. The steady
        # stream tells the Arduino the Pi is still alive.
        if force or now >= self.next_send:
            self.ser.write(line.encode("ascii") + b"\n")
            self.next_send = now + self.period
        self._read()

    def _read(self) -> None:
        # Prints anything the Arduino sends back, one whole line at a time.
        self.rx += self.ser.read(256)
        while b"\n" in self.rx:
            raw, self.rx = self.rx.split(b"\n", 1)
            if self.echo:
                print("[arduino]", raw.decode("ascii", "replace").rstrip())

    def close(self) -> None:
        self.ser.close()


def protocol_line(state: State, pan: float, tilt: float) -> str:
    # The message the Arduino reads: status, left-right angle, up-down angle.
    # Example: "T,117,90". T = tracking, H = briefly missed, L = no face.
    return f"{state.value},{round(pan)},{round(tilt)}"


def draw_preview(frame, faces, result, pan, tilt, fps, mirror):
    """Debug view. Mirroring only flips the picture; the numbers shown and sent
    are always in the camera's own (unmirrored) coordinates."""
    import cv2

    h, w = frame.shape[:2]
    img = cv2.flip(frame, 1) if mirror else frame.copy()

    def px(x):  # Camera x -> display x.
        return int(w - x) if mirror else int(x)

    cv2.drawMarker(img, (w // 2, h // 2), (128, 128, 128), cv2.MARKER_CROSS, 20, 1)
    for f in faces:
        x1, x2 = sorted((px(f.x), px(f.x + f.w)))
        cv2.rectangle(img, (x1, int(f.y)), (x2, int(f.y + f.h)), (160, 160, 160), 1)

    # Green = tracking, yellow = holding, red = lost (OpenCV colors are BGR).
    colors = {State.TRACKING: (0, 255, 0), State.HOLDING: (0, 220, 255), State.LOST: (0, 0, 255)}
    color = colors[result.state]
    if result.face is not None:
        f = result.face
        x1, x2 = sorted((px(f.x), px(f.x + f.w)))
        cv2.rectangle(img, (x1, int(f.y)), (x2, int(f.y + f.h)), color, 2)
    if result.x is not None:
        center = (px(result.x / 1000 * w), int(result.y / 1000 * h))
        cv2.circle(img, center, 6, color, -1)

    coords = "--- , ---" if result.x is None else f"{result.x:4.0f}, {result.y:4.0f}"
    lines = [
        f"{result.state.name}  x,y = {coords}",
        f"pan {pan:5.1f}  tilt {tilt:5.1f}  faces {len(faces)}  {fps:4.1f} fps",
    ]
    if mirror:
        lines.append("preview mirrored (coordinates are not)")
    for i, text in enumerate(lines):
        (tw, th), _ = cv2.getTextSize(text, cv2.FONT_HERSHEY_SIMPLEX, 0.6, 1)
        y = 22 + 22 * i
        cv2.rectangle(img, (4, y - th - 5), (12 + tw, y + 6), (0, 0, 0), -1)
        cv2.putText(img, text, (8, y), cv2.FONT_HERSHEY_SIMPLEX, 0.6, color, 1)
    cv2.imshow("The Watcher (q to quit)", img)
    return cv2.waitKey(1) & 0xFF


def list_ports() -> None:
    from serial.tools import list_ports as lp

    ports = list(lp.comports())
    if not ports:
        print("No serial ports found. Is the Arduino plugged in?")
    for p in ports:
        print(f"{p.device:20} {p.description}")
    by_id = Path("/dev/serial/by-id")
    if by_id.exists():
        print("\nStable names (these do not change between reboots):")
        for link in sorted(by_id.iterdir()):
            print(f"  {link}")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--config", type=Path, default=HERE / "watcher.toml")
    ap.add_argument("--preview", action="store_true", help="show the debug window (needs the desktop)")
    ap.add_argument("--mirror-preview", action="store_true", help="mirror the debug window like a selfie view")
    ap.add_argument("--serial", metavar="PORT", help="send angles to the Arduino on this port")
    ap.add_argument("--list-ports", action="store_true", help="list serial ports and exit")
    args = ap.parse_args()

    if args.list_ports:
        list_ports()
        return

    # Build each part from the settings in watcher.toml.
    cfg = load_config(args.config)
    cam = cfg["camera"]
    tracker = FaceTracker(build(TrackerSettings, cfg["tracking"]), cam["width"], cam["height"])
    pan = AxisMapper(build(AxisSettings, cfg["pan"]))    # left-right servo
    tilt = AxisMapper(build(AxisSettings, cfg["tilt"]))  # up-down servo
    detector = YuNetDetector(cfg["detector"], cam["width"], cam["height"])
    # Only talk to the Arduino if a port was given with --serial.
    link = SerialLink(args.serial, cfg["serial"]) if args.serial else None
    picam2 = open_camera(cam)

    print_period = 1.0 / cfg["output"]["print_hz"]
    next_print = 0.0
    fps, last = 0.0, time.monotonic()
    state = None
    print("Running. Ctrl+C to stop.")
    try:
        # Main loop: runs once per camera frame (about 30 times a second).
        while True:
            # 1. Take a picture.
            frame = picam2.capture_array("main")
            now = time.monotonic()
            fps = 0.9 * fps + 0.1 / max(now - last, 1e-6)  # averaged frame rate
            last = now

            # 2. Find all faces in the picture.
            faces = detector.detect(frame)
            # 3. Pick one person and keep following them. Gives TRACKING,
            #    HOLDING or LOST, plus a smoothed position from 0 to 1000.
            result = tracker.update(faces, now)
            # 4. Turn that position into servo angles (gain, limits, reverse).
            pan_deg = pan.target(result.x)
            tilt_deg = tilt.target(result.y)
            line = protocol_line(result.state, pan_deg, tilt_deg)

            # 5. Send the angles to the Arduino.
            changed = result.state != state
            state = result.state
            if link:
                link.maybe_send(line, now, force=changed)  # State changes go out at once.
            # 6. Print a status line a few times a second (print_hz).
            if changed or now >= next_print:
                coords = "  ---,  ---" if result.x is None else f"{result.x:5.0f},{result.y:5.0f}"
                print(f"{result.state.name:8} x,y={coords}  pan={pan_deg:5.1f}  "
                      f"tilt={tilt_deg:5.1f}  faces={len(faces)}  {fps:4.1f} fps  -> {line}")
                next_print = now + print_period
            # 7. Show the debug window if --preview was used. q or Esc quits.
            if args.preview:
                key = draw_preview(frame, faces, result, pan_deg, tilt_deg, fps, args.mirror_preview)
                if key in (ord("q"), 27):
                    break
    except KeyboardInterrupt:
        pass
    finally:
        # On exit, send "L" so the eye goes back to center, then clean up.
        if link:
            link.maybe_send(protocol_line(State.LOST, pan.s.center_deg, tilt.s.center_deg),
                            time.monotonic(), force=True)
            link.close()
        picam2.stop()
        print("\nStopped.")


if __name__ == "__main__":
    main()

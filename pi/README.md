# The Watcher: Raspberry Pi vision

Finds faces with the Pi camera, locks on to one person, and turns their position
into eye servo angles. Prints them in the terminal and, optionally, sends them
to the Arduino over USB. The Arduino side is described in
[PROTOCOL.md](PROTOCOL.md).

| File | What it does |
|---|---|
| `watcher.py` | Main program: camera, detector, terminal/serial output, debug preview. |
| `tracking.py` | Face selection, smoothing, HOLD/LOST logic, angle mapping. No camera needed. |
| `watcher.toml` | All adjustable settings. |
| `setup.sh` | Checks OS/Python/package versions and downloads the face model. |
| `test_tracking.py` | Tests for `tracking.py`: `python3 -m unittest -v` |

## Detector choice: YuNet (OpenCV)

| | OpenCV YuNet | MediaPipe Face Detector |
|---|---|---|
| Install on Pi OS | `apt install python3-opencv`, which works directly with apt's Picamera2 | pip only, needs a venv with system site packages to reach Picamera2 |
| ARM64 / Python | Debian arm64 package for the system Python | Latest 1.2.0 has a `py3-none` aarch64 wheel, but it was uploaded 27 Oct 2026, today |
| Range | One model with multi-scale outputs; finds small, far faces | Python model is BlazeFace *short-range*, designed for faces within about 2 m (selfie distance) |
| Angles | Trained on WIDER FACE; handles turned and tilted faces much better than Haar | Good for faces roughly facing the camera |
| Code license | OpenCV: Apache 2.0 | Apache 2.0 |
| Model license | **MIT** (opencv_zoo) | Apache 2.0 (Google model card) |

**Choice: YuNet.** It installs from Raspberry Pi OS packages without a venv, keeps
Picamera2 working as-is, and suits a person walking past at a few meters. It is
also a ~230 KB model that is fast on a Pi 5. Neither option's runtime is MIT; OpenCV's
Apache 2.0 license is similarly permissive. The model itself is MIT.

Other licenses used: Picamera2 BSD-2-Clause, pyserial BSD-3-Clause.

### Compatibility, checked 27 Oct 2026

- **Raspberry Pi OS Trixie** (Debian 13, Python 3.13): `python3-opencv` 4.10 on
  arm64. YuNet 2023mar needs OpenCV ≥ 4.8. **Works with apt packages only.**
- **Raspberry Pi OS Bookworm** (Debian 12, Python 3.11): `python3-opencv` is
  4.6, which is too old for the 2023mar model. Either upgrade to Trixie (reflash;
  recommended), or install a newer OpenCV in a venv:
  ```bash
  python3 -m venv --system-site-packages ~/watcher-venv
  ~/watcher-venv/bin/pip install "opencv-python==4.10.0.84"
  ```
  Then run with `~/watcher-venv/bin/python watcher.py`.
- Python 3.11+ is required (`tomllib`).

I couldn't check your Pi directly. `./setup.sh` prints your exact OS, Python,
architecture, and package versions, and says what's missing.

## Setup on the Pi

```bash
cd ~/path/to/Halloween\ Project/pi
chmod +x setup.sh
./setup.sh install      # apt-installs picamera2, opencv, pyserial; downloads model
python3 -m unittest -v  # logic tests
```

The model is checked against a SHA-256 hash after download. `models/` isn't
committed to git.

## Step 1: terminal test (no Arduino)

```bash
python3 watcher.py
```

```
TRACKING x,y=  612,  431  pan= 93.4  tilt= 90.0  faces=1  28.9 fps  -> T,93,90
HOLDING  x,y=  612,  431  pan= 93.4  tilt= 90.0  faces=0  29.1 fps  -> H,93,90
LOST     x,y=  ---,  ---  pan= 90.0  tilt= 90.0  faces=0  29.0 fps  -> L,90,90
```

The last column is exactly what would be sent to the Arduino.

## Step 2: debug preview

Run this on the Pi desktop or over VNC; it needs a screen:

```bash
python3 watcher.py --preview
python3 watcher.py --preview --mirror-preview
```

- Gray boxes: every face found. Thick box: the selected face.
- Dot: the smoothed center being used. Gray cross: image center (500,500).
- Color: **green** = TRACKING, **yellow** = HOLDING, **red** = LOST.
- Press `q` to quit.

### Mirroring: why the preview may look backwards

The camera image is not mirrored. If you face the camera and step to **your**
right, you move toward the **left** of the image, so X gets smaller. That's
correct: from the camera's (and the eye's) point of view, you moved to its left.

A mirror or selfie view feels more natural to stand in front of. That's all
`--mirror-preview` does: it flips the **picture only**. The numbers on screen,
in the terminal, and sent to the Arduino stay in camera coordinates. Don't
"fix" the direction by mirroring; if the eye turns the wrong way, set
`reverse = true` in `[pan]`.

`hflip`/`vflip` in `[camera]` are different: they change the real image, and
therefore the coordinates. Use them only if the camera is physically mounted
rotated, for example upside down (set both to true).

## Step 3: serial to the Arduino

```bash
python3 watcher.py --list-ports
python3 watcher.py --serial /dev/ttyACM0
```

The port is never guessed. Prefer the `/dev/serial/by-id/...` name shown by
`--list-ports`, since it stays the same between reboots. If you get "permission
denied", run `sudo usermod -aG dialout $USER`, then log out and back in. The
Uno restarts when the port opens, so the script waits for the sketch's `READY`
line before sending. Upload the eye firmware first; see [../eye/README.md](../eye/README.md).

## Calibrating the eye

The camera is fixed, so each X position is one gaze direction. The script
computes an **absolute** angle:

```
offset = atan((x - 500) / 500 × tan(fov / 2)) × gain
angle  = center_deg ± offset, then clamped to min_deg..max_deg
```

It does not keep nudging the servo until the face is centered. That only works
when the camera moves with the eye. The same face position always gives the
same angle.

1. **Field of view.** Use the lens's *horizontal* FOV for `[pan] fov_deg`. The IMX708
   standard lens is 66° × 41°; the wide version is 102° × 67°. Check your
   Arducam B030901 listing. Settings use the full-field 2304×1296 sensor mode
   at 16:9. A 4:3 size would crop the sides and make the FOV smaller.
2. **Start narrow.** The defaults allow only 80–100°. Check that the mechanism
   moves freely across that range first.
3. **Direction.** Stand at one side. If the eye looks away from you, set
   `reverse = true`.
4. **Center.** Stand at x ≈ 500. Adjust `center_deg` until the eye looks
   straight at you.
5. **Widen, then gain.** Gradually widen `min_deg`/`max_deg`. Stand near the
   edge of the image. If the eye undershoots, raise `gain`; if it overshoots,
   lower it.
6. **Parallax.** The eye isn't exactly where the camera is. Close-up visitors
   will be slightly off; mount the camera as close to the eye as you can. Gain
   is a good-enough fix at typical viewing distances.
7. **Tilt (optional).** Set `[tilt] enabled = true` and repeat steps 2–5. Y
   increases downward.

## Tuning

| Setting | Problem it fixes |
|---|---|
| `detector.score_threshold` | Lower (0.5–0.6) if faces are missed at angles; raise if things that aren't faces are detected. |
| `tracking.smoothing_s` | Raise if the eye is jittery; lower if it lags behind. |
| `tracking.hold_time_s` | Raise if the eye gives up during brief misses (turning head). |
| `tracking.match_distance` | Raise if it loses a fast-moving person; lower if it jumps to a neighbor. |
| `tracking.acquire_frames` | Raise if random objects briefly become targets. |
| `pan.deadband_deg` | Raise if the servo buzzes or twitches while the person stands still. |
| `camera.width/height` | Larger (e.g. 960×540) to find farther faces, at lower fps. |

Selection rule: with no target, the largest face wins once it has been seen for
`acquire_frames` frames. After that, the script follows that person,
even if someone bigger appears, until they've been gone for `hold_time_s`.

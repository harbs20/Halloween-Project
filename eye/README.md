# The Watcher: eye servo firmware (Arduino Uno R3)

Receives targets from the Pi (`pi/watcher.py`) over USB serial and moves the
eye. This is a separate sketch from `main/`, so your lighting and joystick
sketch is untouched. Lighting and scare code can be added later in the marked
section of `eye.ino`.

Everything is in `eye.ino`: `setup()`, `loop()`, and one function,
`updateEye()`, which does three things every loop:

1. **Reads** any characters from the Pi without waiting. When a whole line has
   arrived, it checks it with `sscanf`. Only a line like `T,94,90` (status, then
   two angles from 0 to 180) is used. Anything else is ignored.
2. **Decides** where to look. A `T` or `H` line sets the target, kept inside the
   eye limits. If there's no `T`/`H` line for `lostTime` (1.5 s), whether
   because the Pi sent `L` or stopped talking, the target becomes center. Until
   then, the eye stays where it was looking.
3. **Moves** 1 degree toward the target every `trackSpeed` ms (10 ms = 100°/s),
   or every `restSpeed` ms (50 ms = 20°/s) when going back to center. It never
   uses `delay()`.

The Pi already smooths the face position, so the Arduino only limits speed. To
slow the eye down, raise `trackSpeed`.

## Settings (top of `eye.ino`)

| Setting | Default | Meaning |
|---|---|---|
| `servoXPin`, `servoYPin` | 9, 10 | Servo signal pins |
| `useEyeY` | `false` | Turn on the up-down servo |
| `eyeLeft`, `eyeRight` | 80, 100 | Left-right limits. The eye is never moved past these. |
| `eyeDown`, `eyeUp` | 85, 95 | Up-down limits |
| `eyeCenterX`, `eyeCenterY` | 90, 90 | Straight ahead; where the eye rests |
| `reverseX`, `reverseY` | `false` | Flip if the eye moves the wrong way |
| `trackSpeed`, `restSpeed` | 10, 50 | Milliseconds per degree |
| `lostTime` | 1500 | Milliseconds without a face before returning to center |

Keep these in sync with `pi/watcher.toml`:
- **Limits:** the Arduino limits are the hard safety limits. Set the Pi's
  `min_deg`/`max_deg` the same or narrower.
- **Center:** use the same `center_deg` on both.
- **Reverse:** reverse direction in **one** place only, either `reverseX` here
  or `reverse` in `pi/watcher.toml`, never both. The Pi is easier: no re-upload.
  This build uses the Pi's `reverse = true` because the eye is upside down.

To add this to `main.ino` later, copy the settings, variables, and
`updateEye()`, call `updateEye()` from `loop()`, and change
`Serial.begin(9600)` to `Serial.begin(115200)`. Remove the joystick
`eyeX.write`/`eyeY.write` lines so they don't fight over the servos.

The Servo library uses Timer1, so `analogWrite()` doesn't work on pins 9 and 10
while servos are attached. Your LED pins 3, 5, and 6 are fine.

## Wiring

```
                      +---------------------------+
 Servo power supply   |  + ----------------+------|---- servo red (V+)
 (sized for servos)   |  - ------------+---|------|---- servo brown/black (GND)
                      +----------------|---|------+
                                       |  [cap] 470-1000 uF across + and -
 Arduino Uno GND  ---------------------+       (near the servos, - to GND)
 Arduino Uno pin 9  ------------------------------------ pan servo signal (orange/yellow/white)
 Arduino Uno pin 10 ------------------------------------ tilt servo signal (optional)
 Arduino Uno USB    ------------------------------------ Raspberry Pi USB port
```

- **Servos get their own power supply.** Do not power them from the Uno's 5V
  pin or from the Pi. Servo current spikes can reset the Uno, brown out the
  Pi, or damage the board's regulator.
- **Common ground:** connect the servo supply's negative (−) to an Uno **GND**
  pin. Without a shared ground the signal has no reference, and the servo
  jitters or ignores it.
- **Never** connect the servo supply's + to the Uno's 5V or VIN pins.
- Put a 470–1000 µF electrolytic capacitor across the servo supply near the
  servos (stripe/− side to GND). It absorbs current spikes.
- The Uno is powered and talks to the Pi over one USB cable. Power the Pi from
  its own official 27 W USB-C supply.

### Servo power for this build: 2 × SG90

An SG90 is rated around 4.8–6 V. Each one can draw roughly 0.7 A when it
starts moving or is pushed against something (stall), so two can briefly pull
about 1.5 A together.

- **Recommended:** a regulated **5 V, 2 A or more** supply, such as a 5 V 2–3 A
  wall adapter with a barrel-jack-to-screw-terminal adapter, or a USB charger
  or power bank feeding a USB breakout board. A breadboard power module is fine
  only if it is rated for 2 A.
- **Also fine:** a 4 × AA **NiMH** rechargeable pack (4.8 V).
- **Avoid:** 4 × AA alkaline (fresh cells are ~6.4 V, above the SG90's
  rating), a 9 V battery (too much voltage, too little current), and the
  Uno's 5V pin (it can't supply the peaks).
- Capacitor: 470–1000 µF rated at 10 V or more.
- Normal jumper wires are fine at this current for short runs.

SG90s have plastic gears and a little slop, so they can buzz while holding
still. If one buzzes at rest, raise `deadband_deg` on the Pi slightly. Don't
drive an SG90 against its end stops; the narrow limits prevent that.

The up-down servo starts turned off. When left-right works, wire the second
SG90 to pin 10, set `useEyeY = true` in `eye.ino` and `[tilt] enabled = true` in
`pi/watcher.toml`, and repeat the test procedure for tilt.

## Building and uploading (from the Mac)

```bash
SKETCH=eye bash scripts/arduino.sh build
SKETCH=eye bash scripts/arduino.sh upload /dev/cu.usbmodem1101
```

In the Arduino IDE, open `eye/eye.ino` instead. Uploading `main/` again later
replaces this firmware; the Uno only runs one sketch at a time.

## Test procedure

Do the first tests with the **eye linkage disconnected** (servo horn free) and
the default 80–100° limits.

**1. Power-up.** Upload, connect servo power, and reset the Uno. The horn
should go to 90° and stay there.

**2. Serial Monitor (no Pi).** Open the Serial Monitor at 115200 baud with
"Newline" line ending. You should see `READY`. Then type:

| Type | Expect |
|---|---|
| `T,95,90` | Horn turns 5°, waits about 1.5 s, then slowly returns to 90. |
| `T,150,90` | Horn stops at 100 (the limit), not 150. |
| `T,0,90` | Horn stops at 80. |
| `hello`, `T,200,90`, `T,95` | No movement. |

Typing is slower than the Pi, so each typed `T` line only tracks for 1.5 s.
That is expected.

**3. With the Pi.** Close the Serial Monitor (only one program can use the port
at a time). Plug the Uno into the Pi and run:

```bash
python3 watcher.py --list-ports
python3 watcher.py --preview --serial /dev/serial/by-id/usb-Arduino...
```

You should see `[arduino] READY`. Step in front of the camera and walk side
to side: the horn follows, and the terminal's `pan=` matches.

**4. Failure checks.** While it is tracking:
- Unplug the USB cable. The eye should hold for about 1.5 s, then return slowly to rest.
- Press Ctrl+C on the Pi. Same result.
- Walk out of view. Same result.

**5. Direction and linkage.** If the horn turns away from you, set
`reverseX = true` and upload again. Then connect the linkage with
the servo at rest and the eye looking straight ahead. Move the eye gently by
hand to check it clears the CAD parts across 80–100°. Widen `eyeLeft`/`eyeRight`
a few degrees at a time, here and in `pi/watcher.toml`, testing each time.

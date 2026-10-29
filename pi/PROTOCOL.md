# Pi → Arduino serial protocol

USB serial, **115200 baud, 8N1**, plain ASCII. The Pi sends one line at a time;
each line ends with `\n` (newline, byte 10).

```
<STATE>,<PAN>,<TILT>\n
```

| Field | Values | Meaning |
|---|---|---|
| `STATE` | `T` | TRACKING: the selected face was detected in this frame. |
| | `H` | HOLDING: face missed briefly. Angles are its last smoothed position. |
| | `L` | LOST: no target. Angles are the configured center angles. |
| `PAN` | integer degrees | Horizontal eye servo target, already clamped to `[pan]` min/max. |
| `TILT` | integer degrees | Vertical eye servo target. If `[tilt] enabled = false`, this is always the tilt center angle. |

Examples:

```
T,94,90
H,94,90
L,90,90
```

Timing:

- About **20 lines per second** (`send_hz`), sent even when nothing has changed.
  This steady stream is a heartbeat: silence means something is wrong.
- A change of state is sent immediately.
- When `watcher.py` stops normally it sends one final `L` line. If it crashes
  or the cable is unplugged, nothing is sent. The Arduino has to notice the silence.
- Startup: opening the port restarts the Uno. The sketch prints
  `READY` once it is listening. The Pi waits for that line (up to
  `reset_wait_s`) before sending anything.
- The Arduino may print other lines back, e.g. for debugging. With
  `echo = true` the Pi prints them as `[arduino] ...`. The Pi never needs a reply.

The angles are **absolute targets**: "point the eye here". They aren't "move
by this much". If the same face stays still, the same number keeps arriving.

## What the Arduino should enforce independently

The Pi already limits and smooths the angles, but treat its output as a request,
not a command. A bug, a wrong config, or line noise on the Pi side must never
be able to damage the eye. `eye/eye.ino` does all of this in one function, `updateEye()`.

### 1. Reject bad lines

- Collect bytes into a buffer until `\n`. Use a fixed buffer size, such as 32
  bytes. If it fills before a newline arrives, throw the line away. Never write
  past the end of the array.
- The first line after the port opens may be cut off. Accept a line only if it
  has exactly three fields, the state is one of `T`, `H`, `L`, and both numbers
  are whole numbers. Ignore anything else. Don't move on a bad line.
- Ignore `\r` if it appears (some terminals add it).

### 2. Clamp to your own limits

Keep your own `MIN`/`MAX` constants for each servo, based on what the
mechanism and CAD can physically reach. Clamp every received angle to them,
even though the Pi already clamps. The Pi limits are for calibration; the
Arduino limits protect the hardware. Keep the Arduino range equal to or
narrower than what is safe.

### 3. Limit speed

Don't jump straight to a new target. Keep a "current angle" and, on a timer
(using `millis()`, without `delay()`), move it toward the target by at most a
fixed step: for example 2° every 10 ms, which is 200°/s. Write only the current
angle to the servo. This removes jerks from detection noise, protects gears, and
makes the motion look more like a real eye. Start slow and speed it up later.

### 4. Handle stale or missing data

Record `millis()` each time a valid line arrives. If no valid line has arrived
for a timeout, act as if the state is `L`. About 500 ms is a reasonable
timeout: ten missed messages at 20 Hz.

The Uno also restarts when the Pi opens the serial port. `setup()` puts the
servos at their rest position before any data arrives, then prints `READY`.

### 5. Decide what L means

On `L` (or a timeout), the eye can return slowly to center, or the Arduino can
run its own idle "searching" motion. Either way, the Arduino owns this
behavior. The Pi's center angles in `L` lines are only a suggestion.

`H` can be treated like `T`; the angle simply isn't changing. It also tells you
the person may be leaving.

### 6. Scare sequences override tracking

Make the Arduino a small state machine, such as `IDLE`, `TRACKING`, and
`SCARE`. While in `SCARE`:

- Keep reading and validating serial lines so the buffer doesn't overflow, and
  remember the latest target. Don't let the lines drive the servos.
- The scare animation controls the servos, still through the same clamp. Use a
  separate (higher) speed limit only if the mechanism can take it.
- When the scare ends, return to `TRACKING` and ease toward the latest target
  using the normal speed limit. If the data is stale, go to `IDLE`.

The Pi never knows about scares. That keeps all safety and show logic in one
place: the Arduino.

### Testing without the Pi

The protocol is plain text, so you can test your Arduino from the Serial
Monitor (115200 baud, "Newline" line ending). Type `T,95,90`, then `L,90,90`.
Also try invalid input such as `T,500,90` and `hello`. Then stop typing and
confirm the timeout behavior.

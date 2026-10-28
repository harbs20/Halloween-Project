// The Watcher - eye servo firmware
// by @harbs20
// CS8: Engineering - Spooktacular Halloween Project
//
// Receives face-tracking targets from the Raspberry Pi over USB serial
// (pi/watcher.py, format in pi/PROTOCOL.md) and moves the eye servo(s).
// The Arduino has the final say: it clamps every angle to the mechanical
// limits below, limits speed, and returns the eye to rest when tracking stops.

#include <Arduino.h>
#include <Servo.h>
#include "protocol.h"

// ---------------- Settings ----------------

// One servo axis. Angles are in degrees, like the Pi sends.
struct AxisConfig {
  int pin;
  bool enabled;    // false = servo not attached, Pi angles ignored
  int centerDeg;   // straight ahead; reversal mirrors around this angle
  int minDeg;      // mechanical limits: the eye is never driven past these
  int maxDeg;
  int restDeg;     // where the eye goes when nobody is tracked
  bool reverse;    // true if the eye moves the wrong way
};

// Conservative limits for first tests. Widen them only after checking the
// linkage by hand. Use the same center on the Pi ([pan] center_deg).
const AxisConfig panConfig  = {9,  true,  90, 80, 100, 90, false};
const AxisConfig tiltConfig = {10, false, 90, 85, 95,  90, false};

const long baudRate = 115200;          // must match pi/watcher.toml

// Movement speed. The Pi already smooths the target, so this is only a cap
// that stops sudden jumps; it does not add more smoothing on top.
const float trackSpeedDegPerSec = 90.0;
const float returnSpeedDegPerSec = 20.0; // slow, eerie return to rest

const unsigned long serialTimeoutMs = 500; // no valid message for this long = lost
const unsigned long lostHoldMs = 1000;     // stare at the last spot before resting
const unsigned long updateIntervalMs = 10; // servo update rate (100 Hz)
const unsigned long partialLineMs = 100;   // drop an unfinished line after this

// Pulse widths for 0 and 180 degrees. These are the Servo library defaults;
// check your servo's datasheet before widening them.
const int pulseMinUs = 544;
const int pulseMaxUs = 2400;

// ---------------- State ----------------

enum EyeMode {
  MODE_REST,      // no target: at or moving to the rest position
  MODE_TRACKING,  // following the Pi's targets (Pi state T or H)
  MODE_LOST_HOLD  // target just lost: holding still before resting
};

struct Axis {
  const AxisConfig& config;
  float current;  // angle being written to the servo now
  float target;   // angle we are moving toward
  int lastPulse;
  Servo servo;
};

Axis pan = {panConfig};
Axis tilt = {tiltConfig};

EyeMode mode = MODE_REST;
unsigned long modeStartMs = 0;
unsigned long lastUpdateMs = 0;

// Latest valid message from the Pi
TrackMessage latest = {'L', 90, 90};
bool haveMessage = false;
unsigned long lastMessageMs = 0;

// Serial line buffer
char lineBuffer[MESSAGE_MAX_LEN + 1];
unsigned char lineLength = 0;
bool lineTooLong = false;
unsigned long lastByteMs = 0;
unsigned int rejectedLines = 0;
unsigned long lastRejectReportMs = 0;

// ---------------- Future: scare and lighting ----------------
// Scare behavior: set scareActive = true while a scare runs and set
// pan.target / tilt.target from the scare animation in updateScare().
// While it is true, serial messages are still read (so the newest target is
// ready afterwards) but they do not move the eye. Targets still go through
// the mechanical limits in updateServos(). When the scare ends, set
// scareActive = false and tracking resumes at the normal speed limit.
bool scareActive = false;

void updateScare(unsigned long /* now */) {
  // Not implemented yet. Example trigger: a button or IR sensor.
}

// Lighting: called every loop with the current mode, e.g. red while
// MODE_TRACKING, orange while MODE_REST. Keep it non-blocking.
// Note: NeoPixel show() pauses interrupts, which can drop serial bytes; only
// call show() when a color actually changes. Dropped bytes only cost a message.
void updateLighting(unsigned long /* now */, EyeMode /* currentMode */) {
  // Not implemented yet.
}

// ---------------- Servo helpers ----------------

// Keeps an angle inside the mechanical limits.
float clampAngle(const AxisConfig& config, float angle) {
  return constrain(angle, (float)config.minDeg, (float)config.maxDeg);
}

// Converts an angle from the Pi: reversal first, then the mechanical limits.
float fromPi(const AxisConfig& config, int angle) {
  float a = config.reverse ? 2 * config.centerDeg - angle : angle;
  return clampAngle(config, a);
}

// Sends the current angle to the servo in microseconds, which gives smoother
// motion than whole degrees while easing between targets.
void writeServo(Axis& axis) {
  float angle = clampAngle(axis.config, axis.current);
  int pulse = pulseMinUs + (int)((pulseMaxUs - pulseMinUs) * angle / 180.0 + 0.5);
  if (pulse != axis.lastPulse) {
    axis.servo.writeMicroseconds(pulse);
    axis.lastPulse = pulse;
  }
}

void startAxis(Axis& axis) {
  if (!axis.config.enabled) return;
  float rest = clampAngle(axis.config, axis.config.restDeg);
  axis.current = rest;
  axis.target = rest;
  // Writing before attach() makes the first pulse the rest position, not 90.
  writeServo(axis);
  axis.servo.attach(axis.config.pin, pulseMinUs, pulseMaxUs);
}

// Moves current toward target by at most maxStep degrees.
void stepAxis(Axis& axis, float maxStep) {
  if (!axis.config.enabled) return;
  float error = axis.target - axis.current;
  if (error > maxStep) error = maxStep;
  if (error < -maxStep) error = -maxStep;
  axis.current += error;
  writeServo(axis);
}

// ---------------- Serial input ----------------

void reportRejected(unsigned long now) {
  // At most one report per second so errors cannot flood the serial line.
  if (rejectedLines > 0 && now - lastRejectReportMs >= 1000) {
    Serial.print(F("BAD "));
    Serial.println(rejectedLines);
    rejectedLines = 0;
    lastRejectReportMs = now;
  }
}

void handleLine(unsigned long now) {
  lineBuffer[lineLength] = '\0';
  TrackMessage msg;
  if (!lineTooLong && parseMessage(lineBuffer, msg)) {
    latest = msg;
    haveMessage = true;
    lastMessageMs = now;
  } else if (lineLength > 0 || lineTooLong) {
    rejectedLines++;
  }
  lineLength = 0;
  lineTooLong = false;
}

// Reads whatever bytes have arrived. Never waits for more.
void readSerial(unsigned long now) {
  // Throw away a line that stopped arriving halfway (e.g. Pi script killed).
  if (lineLength > 0 && now - lastByteMs > partialLineMs) {
    lineLength = 0;
    lineTooLong = false;
    rejectedLines++;
  }

  int budget = 64;  // bounded so a flood of data cannot stall the servos
  while (Serial.available() > 0 && budget-- > 0) {
    char c = Serial.read();
    lastByteMs = now;
    if (c == '\r') continue;
    if (c == '\n') {
      handleLine(now);
    } else if (lineLength < MESSAGE_MAX_LEN) {
      lineBuffer[lineLength++] = c;
    } else {
      lineTooLong = true;  // keep discarding until the next newline
    }
  }
}

// ---------------- Eye behavior ----------------

void setMode(EyeMode newMode, unsigned long now, const __FlashStringHelper* reason) {
  if (newMode == mode) return;
  mode = newMode;
  modeStartMs = now;
  Serial.print(F("MODE "));
  Serial.println(reason);
}

void updateMode(unsigned long now) {
  if (scareActive) return;  // the scare owns the targets

  bool fresh = haveMessage && now - lastMessageMs <= serialTimeoutMs;
  bool piTracking = fresh && (latest.state == 'T' || latest.state == 'H');

  if (piTracking) {
    setMode(MODE_TRACKING, now, F("TRACKING"));
    pan.target = fromPi(pan.config, latest.pan);
    tilt.target = fromPi(tilt.config, latest.tilt);
  } else if (mode == MODE_TRACKING) {
    // Freeze where the eye is right now.
    setMode(MODE_LOST_HOLD, now, fresh ? F("HOLD lost") : F("HOLD timeout"));
    pan.target = pan.current;
    tilt.target = tilt.current;
  } else if (mode == MODE_LOST_HOLD && now - modeStartMs >= lostHoldMs) {
    setMode(MODE_REST, now, F("REST"));
    pan.target = clampAngle(pan.config, pan.config.restDeg);
    tilt.target = clampAngle(tilt.config, tilt.config.restDeg);
  }
}

void updateServos(unsigned long now) {
  unsigned long elapsed = now - lastUpdateMs;
  if (elapsed < updateIntervalMs) return;
  lastUpdateMs = now;
  if (elapsed > 100) elapsed = 100;  // avoid a big jump after a long pause

  float speed = (mode == MODE_REST) ? returnSpeedDegPerSec : trackSpeedDegPerSec;
  float maxStep = speed * elapsed / 1000.0;
  stepAxis(pan, maxStep);
  stepAxis(tilt, maxStep);
}

// ---------------- Main ----------------

void setup() {
  // Servos first, so the eye holds its rest position from power-up.
  startAxis(pan);
  startAxis(tilt);

  Serial.begin(baudRate);
  // The Uno restarts whenever the Pi opens the port. The Pi waits for this
  // line before sending, so no messages are lost during startup.
  Serial.println(F("READY watcher-eye"));
  lastUpdateMs = millis();
}

void loop() {
  unsigned long now = millis();
  readSerial(now);
  updateScare(now);
  updateMode(now);
  updateServos(now);
  updateLighting(now, mode);
  reportRejected(now);
}

// The Watcher - eye servo code
// by @harbs20
// CS8: Engineering - Spooktacular Halloween Project
//
// The Raspberry Pi sends a line like "T,94,90" about 20 times a second:
//   T = tracking a face, H = face briefly missed, L = no face
//   then the left-right angle and the up-down angle.
// updateEye() reads those lines and moves the eye there, slowly and safely.

#include <Servo.h>

Servo eyeX; // left-right servo
Servo eyeY; // up-down servo

const int servoXPin = 9;
const int servoYPin = 10;
const bool useEyeY = false; // set to true when the up-down servo is added

// Eye movement limits. The eye is never moved past these.
const int eyeLeft = 50;
const int eyeRight = 140;
const int eyeDown = 85;
const int eyeUp = 95;

// Where the eye looks when nobody is there
const int eyeCenterX = 90;
const int eyeCenterY = 90;

// Set to true if the eye moves the wrong way
const bool reverseX = true;
const bool reverseY = false;

// Speed: milliseconds per 1 degree of movement
const int trackSpeed = 4;  // following a face (250 degrees per second)
const int restSpeed = 50;  // going back to center (20 degrees per second)

// No face (or no message from the Pi) for this long = go back to center
const unsigned long lostTime = 1500;

int eyeXPosition = eyeCenterX; // where the eye is now
int eyeYPosition = eyeCenterY;
int targetX = eyeCenterX;      // where the eye is going
int targetY = eyeCenterY;
unsigned long lastFaceTime = 0;
unsigned long lastMoveTime = 0;

char message[12];       // the line being received from the Pi
int messageLength = 0;

void setup() {
  // Setting the angle before attach() stops the eye jumping on startup
  eyeX.write(eyeCenterX);
  eyeX.attach(servoXPin);
  if (useEyeY) {
    eyeY.write(eyeCenterY);
    eyeY.attach(servoYPin);
  }

  // The Uno restarts when the Pi connects. The Pi waits for this line.
  Serial.begin(115200);
  Serial.println("READY");
}

void loop() {
  updateEye();

  // Lighting, sound, and scares can go here later.
  // During a scare, skip updateEye() and move the eye yourself.
}

void updateEye() {
  unsigned long currentTime = millis();

  while (Serial.available() > 0) {
    char c = Serial.read();

    if (c != '\n') {
      if (c != '\r' && messageLength < 11) {
        message[messageLength] = c;
        messageLength++;
      }
      continue;
    }

    message[messageLength] = '\0';
    messageLength = 0;

    char state;
    char extra;
    int x;
    int y;
    int found = sscanf(message, "%c,%3d,%3d%c", &state, &x, &y, &extra);

    // Use it only if it has exactly 3 parts, sensible angles, and a face
    if (found == 3 && x >= 0 && x <= 180 && y >= 0 && y <= 180) {
      if (state == 'T' || state == 'H') {
        if (reverseX) x = 2 * eyeCenterX - x;
        if (reverseY) y = 2 * eyeCenterY - y;
        targetX = constrain(x, eyeLeft, eyeRight);
        targetY = constrain(y, eyeDown, eyeUp);
        lastFaceTime = currentTime;
      }
    }
  }

  bool lost = currentTime - lastFaceTime > lostTime; // if face lost
  if (lost) {
    targetX = eyeCenterX;
    targetY = eyeCenterY;
  }

  int speed = trackSpeed;
  if (lost) {
    speed = restSpeed;
  }
  if (currentTime - lastMoveTime >= (unsigned long)speed) {
    lastMoveTime = currentTime;

    if (eyeXPosition < targetX) eyeXPosition++;
    if (eyeXPosition > targetX) eyeXPosition--;
    if (eyeYPosition < targetY) eyeYPosition++;
    if (eyeYPosition > targetY) eyeYPosition--;

    eyeX.write(eyeXPosition);
    if (useEyeY) {
      eyeY.write(eyeYPosition);
    }
  }
}



// The Watcher - eye servo code
// by @harbs20
// CS8: Engineering - Spooktacular Halloween Project
//
// The Raspberry Pi sends a line like "T,94,90" about 30 times a second:
//   T = tracking a face, H = face briefly missed, L = no face
//   then the left-right angle and the up-down angle.
// updateEye() reads those lines and moves the eye there

#include <Servo.h>
#include <SoftwareSerial.h>
#include <DFRobotDFPlayerMini.h>

SoftwareSerial softwareSerial(2, 3);

DFRobotDFPlayerMini mp3Player;

Servo eyeX; // left-right servo
Servo eyeY; // up-down servo

const int servoXPin = 9;
const int servoYPin = 10;
const bool useEyeY = true;  // up-down servo on pin 10

// Eye movement limits. The eye is never moved past these.
const int eyeLeft = 50;
const int eyeRight = 140;
const int eyeDown = 70;
const int eyeUp = 110;

// Where the eye looks when nobody is there
const int eyeCenterX = 90;
const int eyeCenterY = 90;

// Set to true if the eye moves the wrong way
const bool reverseX = false;
const bool reverseY = false;

const int trackSpeed = 2;  // ms per degree while tracking (500 degrees per second)
const int restSpeed = 150; // ms per degree going back to center (slow, creepy drift: about 7 degrees per second)

// No face (or no message from the Pi) for this long = go back to center
const unsigned long lostTime = 1500;

int eyeXPosition = eyeCenterX; // where the eye is now
int eyeYPosition = eyeCenterY;
int targetX = eyeCenterX;      // where the eye is going
int targetY = eyeCenterY;
unsigned long lastFaceTime = 0;
unsigned long lastMoveTime = 0;

char message[12];
int messageLength = 0;

bool mp3Ready = false; // false if the MP3 player didn't answer at startup

void setup() {
  // Servos first, so the eye holds still at center no matter what happens next.
  // Setting the angle before attach() stops the eye jumping on startup
  eyeX.write(eyeCenterX);
  eyeX.attach(servoXPin);
  if (useEyeY) {
    eyeY.write(eyeCenterY);
    eyeY.attach(servoYPin);
  }

  Serial.begin(115200);

  // Try the MP3 player once. If it isn't there (unplugged, no SD card),
  // keep going without sound instead of freezing.
  softwareSerial.begin(9600);
  mp3Ready = mp3Player.begin(softwareSerial);

  // The Uno restarts when the Pi connects. The Pi waits for this line.
  Serial.println("READY");

  if (mp3Ready) {
    mp3Player.volume(20);
    mp3Player.play(1);
  } else {
    Serial.println("MP3 player not found, running without sound");
  }
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

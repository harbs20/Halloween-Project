

// The Watcher - eye servo code
// by @harbs20
// CS8: Engineering - Spooktacular Halloween Project
//
// The Raspberry Pi sends a line like "T,94,90" about 30 times a second:
//   T = tracking a face, H = face briefly missed, L = no face
//   then the left-right angle and the up-down angle.
// updateEye() reads those lines and moves the eye there

#include <Adafruit_TiCoServo.h>
#include <SoftwareSerial.h>
#include <DFRobotDFPlayerMini.h>
#include <Adafruit_NeoPixel.h>

SoftwareSerial softwareSerial(2, 3);

DFRobotDFPlayerMini mp3Player;

// TiCoServo makes the servo pulses with the Uno's hardware timer, so the LED
// strip's show() can't stretch them and make the eye twitch.
// It only works on pins 9 and 10.
Adafruit_TiCoServo eyeX; // left-right servo
Adafruit_TiCoServo eyeY; // up-down servo

const int servoXPin = 9;
const int servoYPin = 10;
const bool useEyeY = true;  // up-down servo on pin 10

// Eye movement limits. The eye is never moved past these.
const int eyeLeft = 52;
const int eyeRight = 142;
const int eyeDown = 69;
const int eyeUp = 109;

// Where the eye looks when nobody is there
const int eyeCenterX = 97;
const int eyeCenterY = 89;

// Set to true if the eye moves the wrong way
const bool reverseX = false;
const bool reverseY = false;

const int trackSpeed = 2;  // ms per degree while tracking (500 degrees per second)
const int restSpeed = 100; // ms per degree going back to center (creepy drift: 10 degrees per second, 4-5 s from either side)

// No face (or no message from the Pi) for this long = go back to center
const unsigned long lostTime = 1500;

int eyeXPosition = eyeCenterX; // where the eye is now
int eyeYPosition = eyeCenterY;
int targetX = eyeCenterX;      // where the eye is going
int targetY = eyeCenterY;
unsigned long lastFaceTime = 0;
unsigned long lastMoveTime = 0;



// One LED behind the eye: slow red-orange fade, red pulse when someone is close
const int ledPin = 11;      // not 9 or 10 (the servos use those)
const int ledCount = 1;     // just the one behind the eye
Adafruit_NeoPixel strip(ledCount, ledPin, NEO_GRB + NEO_KHZ800);

const int fadeSpeed = 5;    // ms per step of the red-orange fade (bigger = slower)
const int maxGreen = 100;   // how orange the fade gets (0 = red, 100 = orange)
const int pulseSpeed = 4;   // ms per step of the red pulse (bigger = slower)
const int pulseLow = 20;    // dimmest red during the pulse (0-255)
const int pulseHigh = 255;  // brightest red

int green = 0;               // 0 = red, maxGreen = orange
int fadeDirection = 1;       // 1 = toward orange, -1 = back toward red
int pulseBrightness = pulseLow;
int pulseDirection = 1;
unsigned long lastFadeTime = 0;
unsigned long lastShowTime = 0;

// Ultrasonic distance sensor
const int trigPin = 5;
const int echoPin = 4;
const int scareDistance = 50;          // cm: closer than this starts the red pulse
const unsigned long scareHold = 1000;  // keep pulsing this long after they step back

float dist = 999;  // last measured distance in cm (999 = nobody)
unsigned long lastPingTime = 0;
unsigned long lastCloseTime = 0;

char message[12];
int messageLength = 0;

bool mp3Ready = false; // false if the MP3 player didn't answer at startup

void setup() {
  // Servos first, so the eye holds still at center no matter what happens next.
  // No pulses go out until the first write(), so the eye doesn't jump on startup.
  // Attach both before writing either: attach() resets the timer, which
  // switches off a servo that has already been written to.
  eyeX.attach(servoXPin);
  if (useEyeY) {
    eyeY.attach(servoYPin);
  }
  eyeX.write(eyeCenterX);
  if (useEyeY) {
    eyeY.write(eyeCenterY);
  }

  Serial.begin(115200);

  softwareSerial.begin(9600);
  mp3Ready = mp3Player.begin(softwareSerial);
  
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  
  strip.begin();
  strip.setBrightness(150);
  strip.show();
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
  updateDistance();
  updateLights();
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

// Measures the distance in cm, at most every 60 ms. The 6000 us timeout
// (about 1 m) keeps pulseIn from freezing the eye when nothing echoes back.
void updateDistance() {
  unsigned long currentTime = millis();
  if (currentTime - lastPingTime < 60) return;
  lastPingTime = currentTime;

  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  unsigned long duration = pulseIn(echoPin, HIGH, 6000);
  if (duration == 0) {
    dist = 999;  // no echo = nobody close
  } else {
    dist = duration * 0.0343 / 2;
  }

  if (dist < scareDistance) {
    lastCloseTime = currentTime;
  }
}

void updateLights() {
  unsigned long currentTime = millis();
  bool close = lastCloseTime > 0 && currentTime - lastCloseTime < scareHold;

  if (close) {
    // Pulse solid red between pulseLow and pulseHigh
    if (currentTime - lastFadeTime < (unsigned long)pulseSpeed) return;
    lastFadeTime = currentTime;

    pulseBrightness += pulseDirection * 5;
    if (pulseBrightness >= pulseHigh || pulseBrightness <= pulseLow) {
      pulseDirection = -pulseDirection;
      pulseBrightness = constrain(pulseBrightness, pulseLow, pulseHigh);
    }

    for (int i = 0; i < ledCount; i++) {
      strip.setPixelColor(i, strip.Color(pulseBrightness, 0, 0));
    }
  } else {
    // Normal slow red-orange fade
    if (currentTime - lastFadeTime < (unsigned long)fadeSpeed) return;
    lastFadeTime = currentTime;

    green += fadeDirection;
    if (green >= maxGreen || green <= 0) {
      fadeDirection = -fadeDirection;
    }

    for (int i = 0; i < ledCount; i++) {
      strip.setPixelColor(i, strip.Color(150, green * 150 / 255, 0));  // dimmer fade
    }
  }

  // show() pauses the Arduino for a moment, which can drop bytes from the Pi,
  // so only send the colors to the strip every 20 ms (50 times a second).
  if (currentTime - lastShowTime >= 20) {
    lastShowTime = currentTime;
    strip.show();
  }
}

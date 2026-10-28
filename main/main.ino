// C++ Code
// by @harbs20
// CS8: Engineering - Spooktacular Halloween Project
/* This project includes:
1 Arduino Uno R3
RGB Strip, LEDs, and resistors to illuminate the project for extra spookiness
1 speaker with an .mp3 player wired up (this is not seen in TinkerCAD, so the code for it is commented out
Automatic orange-red-orange Halloween lighting
1 Joystick with 2 potentiometers to control the eye
2 Servos to control eye movement
1 DC motor (shown as a servo in the circuit) to move a passive spider up and down
A few IR sensors (not shown in the circuit) to sense various things such as when someone grabs candy
A few push buttons to control the project
 
(if i get to it):
1 Raspberry Pi and 1 Raspberry Pi Camera to detect faces and move the eyeball of the project to follow the face
*/

// Libraries
#include <Servo.h>
#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// Pin Variables
const int redLed = 3;
const int blueLed = 5;
const int greenLed = 6;

const int speaker = 4;

const int joystickXPin = A3;
const int joystickYPin = A4;

Servo eyeX; // Servo for eye movement
Servo eyeY; // Servo for eye movement

const int servoXPin = 9; // Pin for X-axis servo
const int servoYPin = 10; // Pin for Y-axis servo

// Eye movement limits
const int eyeLeft = 80;
const int eyeRight = 100;
const int eyeDown = 80;
const int eyeUp = 100;

// RGB LED Strip
#define RGB_LED_PIN 2
Adafruit_NeoPixel strip = Adafruit_NeoPixel(10, RGB_LED_PIN, NEO_GRB + NEO_KHZ800);

// Red stays on; a little green mixes with it to make orange
int greenValue = 80;
int colorDirection = -1;
unsigned long lastColorChange = 0;
const int colorInterval = 25; // Milliseconds between fade steps

void setup() {
  pinMode(redLed, OUTPUT);
  pinMode(blueLed, OUTPUT);
  pinMode(greenLed, OUTPUT);
  Serial.begin(9600);

  // Attach the eye servos and start with the eye looking straight ahead
  eyeX.attach(servoXPin);
  eyeY.attach(servoYPin);
  eyeX.write(90);
  eyeY.write(90);

  // Initialize the RGB LED strip
  strip.begin();
  strip.show();
  strip.setBrightness(50); // Set brightness to 20%

}

void loop() {
  // Fade orange to red and back without pausing joystick control
  unsigned long currentTime = millis();
  if (currentTime - lastColorChange >= colorInterval) {
    lastColorChange = currentTime;

    // Set the separate RGB LED to the same color as the strip
    analogWrite(redLed, 255);
    analogWrite(greenLed, greenValue);
    analogWrite(blueLed, 0);

    // Set every pixel to the current Halloween color
    for (int i = 0; i < strip.numPixels(); i++) {
      strip.setPixelColor(i, strip.Color(255, greenValue, 0));
    }
    strip.show();

    // Reverse the fade at red (0) and orange (80)
    if (greenValue <= 0) {
      colorDirection = 1;
    }
    if (greenValue >= 80) {
      colorDirection = -1;
    }
    greenValue = greenValue + colorDirection;
  }

  // Read the two joystick potentiometers
  int joystickXValue = analogRead(joystickXPin);
  int joystickYValue = analogRead(joystickYPin);

  // Map the joystick values to eye angles
  int eyeXPosition = map(joystickXValue, 0, 1023, eyeLeft, eyeRight);
  int eyeYPosition = map(joystickYValue, 0, 1023, eyeDown, eyeUp);

  // Move the eye to match the joystick
  eyeX.write(eyeXPosition);
  eyeY.write(eyeYPosition);

} 


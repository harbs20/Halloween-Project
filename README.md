# Halloween Project

Arduino Uno R3 code for the CS8 Spooktacular Halloween Project.

The Raspberry Pi face tracking for the eye ("The Watcher") is in `pi/`. See
`pi/README.md`. The Pi-to-Arduino serial format is in `pi/PROTOCOL.md`.
The Arduino firmware that receives it and drives the eye servos is the `eye/`
sketch (`SKETCH=eye bash scripts/arduino.sh build`); see `eye/README.md`.

The real project folder is `~/CODE/Halloween Project`. The Dropbox `Code` path
is a symlink to this folder.

## Sketch

Edit `main/main.ino`. Its parent folder matches the sketch name so Arduino IDE
and Arduino CLI can open it directly. The existing project notes and Servo
include are preserved. The 10-pixel NeoPixel strip on pin 2 and the separate
RGB LED on pins 3 (red), 6 (green), and 5 (blue) automatically fade from orange
to red and back in about four seconds. Red stays at 255, blue stays off, and
green varies from 0 to 80 to mix the orange color. A `for` loop updates all
pixels; `millis()` spaces the fade steps without a delay. A0-A2 are now unused.
The separate LED assumes common-cathode wiring with resistors.

The eye servos on pins 9 and 10 start at 90 degrees. A joystick's horizontal
output connects to A3 and its vertical output to A4, with power to 5V and GND.
Each axis directly controls its servo from 80 to 100 degrees without a delay.
Movement limits and joystick direction have not been checked on hardware.
A4 is also the Uno's I2C SDA pin, so this pin assignment will need to change
if the I2C LCD is added.
Sensors, buttons, the spider motor, sound, and the LCD still
need to be added after their wiring and behavior are confirmed.

Target: `arduino:avr:uno`, Arduino AVR Boards 1.8.7.

Libraries: Servo 1.3.0, Adafruit NeoPixel 1.15.5, and LiquidCrystal I2C 1.1.2.
The sketch includes `Adafruit_NeoPixel.h`, `LiquidCrystal_I2C.h`, and the Uno
core's `Wire.h` for I2C. Configure the LED pin/count and LCD address/dimensions
when the wiring is defined.

## VS Code

Open this repository folder and install the recommended extensions if prompted.
Press **Cmd+Shift+B** to compile for the Uno. In **Terminal > Run Task**, use
**Arduino: List Boards** to find the connected board's port, then
**Arduino: Upload Uno** to build and upload to that port.

Use these repository tasks for builds and uploads. They also find Servo in this
Mac's Arduino IDE library directory. The included C++ configuration points to
the installed macOS Uno compiler and headers, including Servo, NeoPixel, and
LiquidCrystal I2C. If VS Code still
shows old include errors, run **Developer: Reload Window**.

## Terminal

The script uses `arduino-cli` from PATH or the copy bundled with Arduino IDE 2
in `/Applications/Arduino IDE.app`.

```sh
# On a new machine, install the board core and libraries (requires internet):
bash scripts/arduino.sh setup

bash scripts/arduino.sh build
bash scripts/arduino.sh boards
bash scripts/arduino.sh upload /dev/cu.usbmodem1101
```

Replace the example port with the one reported for your board. Build output
goes into `.build/uno` and is excluded from Git. On another OS or after changing
toolchain versions, update `.vscode/c_cpp_properties.json` for that installation.

Arduino sketch metadata reference:
https://docs.arduino.cc/arduino-cli/sketch-project-file

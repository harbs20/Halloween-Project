#!/usr/bin/env python3
"""Find the eye's true center by sending it angles by hand.

Run on the Pi with the Uno plugged in (stop the service first, since only one
program can use the port):

    sudo systemctl stop watcher
    python3 center_test.py                   # finds the Arduino by itself
    python3 center_test.py /dev/ttyACM0      # or name the port

The angle you choose is re-sent 30 times a second, so the eye holds still
instead of drifting back to center. Commands (press Enter after each):

    95        left-right to 95
    95 88     left-right to 95, up-down to 88
    a / d     left-right down / up 1 degree
    s / w     up-down down / up 1 degree
    q         quit and print the numbers to put in eye.ino and watcher.toml

The Arduino still enforces its limits, so angles outside eyeLeft..eyeRight or
eyeDown..eyeUp stop at the limit.
"""

import sys
import threading
import time
from pathlib import Path

from watcher import HERE, SerialLink, load_config

HELP = """Commands (press Enter after each):
  95      left-right to 95
  95 88   left-right to 95, up-down to 88
  a / d   left-right down / up 1 degree
  s / w   up-down down / up 1 degree
  q       quit and print the numbers to use"""


def find_port() -> str:
    # Same search as start_watcher.sh: stable by-id name first, then ttyACM0.
    for port in sorted(Path("/dev/serial/by-id").glob("usb-Arduino*")) + [Path("/dev/ttyACM0")]:
        if port.exists():
            return str(port)
    sys.exit("No Arduino found. Plug it in, or give the port: python3 center_test.py /dev/ttyACM0")


def main() -> None:
    cfg = load_config(HERE / "watcher.toml")
    pan = round(cfg["pan"]["center_deg"])
    tilt = round(cfg["tilt"]["center_deg"])

    port = sys.argv[1] if len(sys.argv) > 1 else find_port()
    link = SerialLink(port, cfg["serial"])
    running = True

    def keep_sending():
        # Sends "T,pan,tilt" over and over so the Arduino keeps holding the angle.
        while running:
            link.maybe_send(f"T,{pan},{tilt}", time.monotonic())
            time.sleep(0.005)

    sender = threading.Thread(target=keep_sending, daemon=True)
    sender.start()

    print(HELP)
    try:
        while True:
            print(f"\nNow: left-right {pan}, up-down {tilt}")
            cmd = input("> ").strip().lower()
            parts = cmd.split()
            if cmd == "q":
                break
            elif cmd == "a":
                pan -= 1
            elif cmd == "d":
                pan += 1
            elif cmd == "s":
                tilt -= 1
            elif cmd == "w":
                tilt += 1
            elif 1 <= len(parts) <= 2 and all(p.isdigit() and int(p) <= 180 for p in parts):
                pan = int(parts[0])
                if len(parts) == 2:
                    tilt = int(parts[1])
            else:
                print("Type a number (e.g. 95 or 95 88), a/d, s/w, or q.")
                continue
            pan = max(0, min(180, pan))
            tilt = max(0, min(180, tilt))
    except (KeyboardInterrupt, EOFError):
        pass
    finally:
        running = False
        sender.join()
        link.close()

    print(f"""
If the eye looked straight ahead, use these centers:
  eye.ino:       eyeCenterX = {pan};   eyeCenterY = {tilt};
  watcher.toml:  [pan] center_deg = {pan}   [tilt] center_deg = {tilt}
Shift the limits by the same amount so the eye still turns evenly both ways.
Then: sudo systemctl start watcher""")


if __name__ == "__main__":
    main()

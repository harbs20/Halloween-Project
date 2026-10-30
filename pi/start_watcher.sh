#!/usr/bin/env bash
# Finds the Arduino and starts The Watcher. Used by watcher.service at boot,
# or run it by hand: ./start_watcher.sh
# If no Arduino is plugged in it exits, and the service tries again in 3 s.

cd "$(dirname "$0")"

# The /dev/serial/by-id name stays the same between reboots; ttyACM0 is the fallback.
for port in /dev/serial/by-id/usb-Arduino* /dev/ttyACM0; do
  if [ -e "$port" ]; then
    exec python3 -u watcher.py --serial "$port"
  fi
done

echo "No Arduino found. Is it plugged in?"
exit 1

#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if command -v arduino-cli >/dev/null 2>&1; then
  cli="$(command -v arduino-cli)"
elif [[ -x '/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli' ]]; then
  cli='/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli'
else
  echo 'Install Arduino CLI or Arduino IDE 2, then run this command again.' >&2
  exit 1
fi

sketch="${SKETCH:-main}"  # SKETCH=eye builds the eye firmware
action="${1:-build}"
case "$action" in
  setup)
    "$cli" core update-index
    "$cli" core install arduino:avr@1.8.7
    "$cli" lib install Servo@1.3.0 'Adafruit NeoPixel@1.15.5' 'Adafruit TiCoServo@1.0.6' 'LiquidCrystal I2C@1.1.2'
    ;;
  boards)
    "$cli" board list
    ;;
  build|upload)
    args=(compile --fqbn arduino:avr:uno --warnings all --build-path "$repo_dir/.build/uno")
    # Also find libraries installed by this Mac's Arduino IDE.
    if [[ -d "$HOME/Library/Arduino15/libraries" ]]; then
      args+=(--libraries "$HOME/Library/Arduino15/libraries")
    fi
    if [[ "$action" == upload ]]; then
      if [[ -z "${2:-}" ]]; then
        echo 'Usage: bash scripts/arduino.sh upload <port> (run boards to find the port)' >&2
        exit 1
      fi
      args+=(--upload --port "$2")
    fi
    "$cli" "${args[@]}" "$repo_dir/$sketch"
    ;;
  *)
    echo 'Usage: bash scripts/arduino.sh {setup|build|boards|upload <port>}' >&2
    exit 1
    ;;
esac

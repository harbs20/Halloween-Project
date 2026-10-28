#!/usr/bin/env bash
# Check the Pi's software versions and download the YuNet face model.
# Usage: ./setup.sh          (check + download)
#        ./setup.sh install  (also apt-install the packages it needs)
set -euo pipefail
cd "$(dirname "$0")"

MODEL=models/face_detection_yunet_2023mar.onnx
MODEL_URL=https://github.com/opencv/opencv_zoo/raw/main/models/face_detection_yunet/face_detection_yunet_2023mar.onnx
MODEL_SHA256=8f2383e4dd3cfbb4553ea8718107fc0423210dc964f9f4280604804ed2552fa4

if [[ "${1:-}" == "install" ]]; then
  sudo apt update
  sudo apt install -y python3-picamera2 python3-opencv python3-serial
fi

echo "== System =="
. /etc/os-release && echo "OS:      $PRETTY_NAME"
echo "Arch:    $(uname -m)"
echo "Python:  $(/usr/bin/python3 --version 2>&1)"
grep -m1 Model /proc/cpuinfo 2>/dev/null || true

echo "== Python packages (system python3) =="
/usr/bin/python3 - <<'EOF'
import importlib, sys
ok = True
for name in ("picamera2", "cv2", "numpy", "serial"):
    try:
        m = importlib.import_module(name)
        print(f"{name:10} {getattr(m, '__version__', 'installed')}")
    except Exception as e:
        print(f"{name:10} MISSING ({e})"); ok = False
if sys.version_info < (3, 11):
    print("Python 3.11+ is needed (for tomllib)."); ok = False
try:
    import cv2
    v = tuple(int(p) for p in cv2.__version__.split(".")[:2])
    if not hasattr(cv2, "FaceDetectorYN") or v < (4, 8):
        print(f"OpenCV {cv2.__version__} is too old for the 2023mar YuNet model (needs 4.8+). See README.md.")
        ok = False
except ImportError:
    pass
print("All checks passed." if ok else "Fix the items above, or run: ./setup.sh install")
EOF

echo "== Model =="
mkdir -p models
if [[ ! -f "$MODEL" ]]; then
  echo "Downloading $MODEL ..."
  curl -fL -o "$MODEL.part" "$MODEL_URL"
  mv "$MODEL.part" "$MODEL"
fi
echo "$MODEL_SHA256  $MODEL" | sha256sum -c -

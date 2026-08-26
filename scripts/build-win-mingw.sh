#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SDK_ROOT="${XPLANE_SDK_PATH:-$ROOT/../SDKs/XPlane_SDK}"
BUILD_ROOT="${YAL_AUTOUNICOM_BUILD_ROOT:-$HOME/dev/YAL Auto-Unicom Helper}"
BUILD_DIR="${BUILD_DIR:-$BUILD_ROOT/build-win}"

if [ ! -f "$SDK_ROOT/CHeaders/XPLM/XPLMPlugin.h" ]; then
  echo "X-Plane SDK not found at: $SDK_ROOT" >&2
  exit 1
fi

cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
  -DXPLANE_SDK_PATH="$SDK_ROOT"
cmake --build "$BUILD_DIR"

STAGE="$ROOT/deploy/YAL_AutoUnicomHelper"
mkdir -p "$STAGE/64" "$STAGE/resources"
cp -f "$BUILD_DIR/win.xpl" "$STAGE/64/win.xpl"
cp -f "$ROOT/resources/auto_unicom_chime.wav" "$STAGE/resources/auto_unicom_chime.wav"
echo "Staged: $STAGE"

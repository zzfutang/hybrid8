#!/bin/bash
#
# build.sh — regenerate the Xcode project and build the app + AUv3 extension.
# Requires full Xcode (not just Command Line Tools).
#
set -euo pipefail
cd "$(dirname "$0")/.."

# Use full Xcode even if `xcode-select` still points at Command Line Tools.
if ! xcode-select -p 2>/dev/null | grep -q "Xcode.app"; then
  if [ -d /Applications/Xcode.app ]; then
    export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
    echo "==> Using DEVELOPER_DIR=$DEVELOPER_DIR"
  else
    echo "ERROR: full Xcode not found in /Applications. Install it first." >&2
    exit 1
  fi
fi

echo "==> Regenerating Xcode project"
xcodegen generate

CONFIG="${1:-Release}"

echo "==> Building ($CONFIG)"
xcodebuild \
  -project Analog8.xcodeproj \
  -scheme Analog8 \
  -configuration "$CONFIG" \
  -derivedDataPath build \
  CODE_SIGN_IDENTITY="-" \
  CODE_SIGN_STYLE=Manual \
  clean build

APP="build/Build/Products/$CONFIG/Analog8.app"
echo "==> Built: $APP"
ls -d "$APP"

#!/bin/bash
#
# install.sh — build, install the host app to /Applications (which registers
# the bundled AUv3 extension with the system), then validate with auval.
#
set -euo pipefail
cd "$(dirname "$0")/.."

CONFIG="Release"
./scripts/build.sh "$CONFIG"

APP="build/Build/Products/$CONFIG/Analog8.app"
DEST="/Applications/Analog8.app"

echo "==> Installing to $DEST"
rm -rf "$DEST"
cp -R "$APP" "$DEST"

echo "==> Launching once to register the Audio Unit extension"
open "$DEST"
sleep 6

echo "==> Registered audio-unit extensions (pluginkit):"
pluginkit -m -p com.apple.AudioUnit-UI 2>/dev/null | grep -i analog || \
  pluginkit -mv 2>/dev/null | grep -i analog || echo "  (not listed yet — see notes)"

echo
echo "==> Validating with auval (type=aumu subtype=An8v manufacturer=Jhgn)"
auval -v aumu An8v Jhgn || {
  echo "auval reported issues — see output above."
  exit 1
}

echo
echo "==> SUCCESS. In Logic Pro the instrument appears under:"
echo "    AU Instruments > Johan > Analog 8   (or: Johan: Analog 8)"
echo "    If it doesn't show, restart Logic so it re-scans plug-ins."

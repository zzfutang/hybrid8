#!/bin/bash
#
# install-r50.sh — build, install the R50 host app to /Applications (which
# registers the bundled AUv3 extension with the system), then validate with
# auval.
#
set -euo pipefail
cd "$(dirname "$0")/.."

CONFIG="Release"
./scripts/build-r50.sh "$CONFIG"

APP="build/Build/Products/$CONFIG/R50.app"
DEST="/Applications/R50.app"

echo "==> Closing any running R50 container"
osascript -e 'quit app "R50"' 2>/dev/null || true
for _ in 1 2 3 4 5; do
  pgrep -x R50 >/dev/null 2>&1 || break
  sleep 1
done
if pgrep -x R50 >/dev/null 2>&1; then
  killall R50 2>/dev/null || true
fi

echo "==> Installing to $DEST"
rm -rf "$DEST"
cp -R "$APP" "$DEST"

echo "==> Launching once to register the Audio Unit extension"
open "$DEST"
sleep 6

echo "==> Registered audio-unit extensions (pluginkit):"
pluginkit -m -p com.apple.AudioUnit-UI 2>/dev/null | grep -i R50 || \
  pluginkit -mv 2>/dev/null | grep -i R50 || echo "  (not listed yet — see notes)"

echo
echo "==> Validating with auval (type=aumu subtype=R50v manufacturer=Jhgn)"
auval -v aumu R50v Jhgn || {
  echo "auval reported issues — see output above."
  exit 1
}

echo "==> Relaunching the freshly installed standalone container"
osascript -e 'quit app "R50"' 2>/dev/null || true
sleep 1
open "$DEST"

echo
echo "==> SUCCESS. In Logic Pro the instrument appears under:"
echo "    AU Instruments > Rytell > R50   (or: Rytell: R50)"
echo "    If it doesn't show, restart Logic so it re-scans plug-ins."

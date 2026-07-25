#!/bin/bash
#
# package.sh — build a universal Release and zip it for sharing with another
# Mac user. The app is only ad-hoc signed (no Apple Developer account), so it is
# NOT notarized: the recipient must clear the download quarantine once and then
# launch the app to register the AUv3. dist/INSTALL.txt spells this out for them.
#
set -euo pipefail
cd "$(dirname "$0")/.."

CONFIG="Release"
./scripts/build.sh "$CONFIG"

APP="build/Build/Products/$CONFIG/Hybrid8.app"
OUT="dist"
ZIP="$OUT/Hybrid8.zip"

echo "==> Verifying universal binary (arm64 + x86_64)"
lipo -info "$APP/Contents/MacOS/Hybrid8"

mkdir -p "$OUT"

cat > "$OUT/INSTALL.txt" <<'TXT'
Hybrid 8 — install on macOS
===========================

Hybrid 8 is an Audio Unit (AUv3) instrument bundled inside a small host app.
It is not notarized by Apple, so macOS quarantines it after download. Clear
that once, then launch the app so the plug-in registers with the system.

1. Move Hybrid8.app to /Applications.

2. Open Terminal and run (copy-paste the whole line):

       xattr -dr com.apple.quarantine /Applications/Hybrid8.app

3. Double-click Hybrid8.app once. It registers the Audio Unit and opens as a
   standalone player. You can quit it after it launches.

4. In your DAW, rescan/refresh plug-ins if needed. In Logic Pro:
       Software Instrument track > Instrument slot >
       AU Instruments > Rytell > Hybrid 8
   If it doesn't appear, quit and reopen the DAW so it re-scans Audio Units.

Requirements
------------
- macOS 13 (Ventura) or later
- Apple Silicon or Intel (the app ships both architectures)

Why the Terminal step?
----------------------
Notarizing an app to skip it requires a paid Apple Developer account. Without
that, macOS treats the download as untrusted; the xattr command just removes
the quarantine flag so it will launch.
TXT

echo "==> Zipping (ditto preserves the code signature)"
rm -f "$ZIP"
ditto -c -k --keepParent "$APP" "$ZIP"

echo
echo "==> Done."
echo "    App:     $APP"
echo "    Archive: $ZIP"
echo "    Notes:   $OUT/INSTALL.txt"
echo
echo "Send both Hybrid8.zip and INSTALL.txt to the other user."

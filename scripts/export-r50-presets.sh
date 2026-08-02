#!/bin/bash
#
# export-r50-presets.sh — dump the installed R50 AU's factory bank as JSON
# patch documents into Products/R50/factory_presets/. Run after changing the
# Swift recipes (or after editing patches in the app and exporting the ones
# you want to keep), then rebuild: the bundled files are the shipping bank.
#
set -euo pipefail
cd "$(dirname "$0")/.."

echo "==> Exporting factory presets from the installed R50 AU"
SWIFT_MODULE_CACHE=/tmp/r50-preset-export-module-cache
mkdir -p "$SWIFT_MODULE_CACHE"
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer \
CLANG_MODULE_CACHE_PATH="$SWIFT_MODULE_CACHE" \
    xcrun swiftc Tools/R50PresetExport.swift \
    -module-cache-path "$SWIFT_MODULE_CACHE" \
    -framework AVFoundation -framework AudioToolbox \
    -o /tmp/r50_preset_export

/tmp/r50_preset_export "${1:-Products/R50/factory_presets}"

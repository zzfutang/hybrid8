#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."

OUTPUT="${1:-build/preset-audit}"
mkdir -p "$OUTPUT"

echo "==> Compiling R50 AU preset audit"
SWIFT_MODULE_CACHE=/tmp/r50-preset-audit-module-cache
mkdir -p "$SWIFT_MODULE_CACHE"
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer \
CLANG_MODULE_CACHE_PATH="$SWIFT_MODULE_CACHE" \
    xcrun swiftc Tools/R50PresetAudit.swift \
    -module-cache-path "$SWIFT_MODULE_CACHE" \
    -framework AVFoundation -framework AudioToolbox -framework Accelerate \
    -o /tmp/r50_preset_audit

echo "==> Rendering and analysing 100 factory presets"
/tmp/r50_preset_audit "$OUTPUT"

#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."

OUTPUT="${1:-build/signal-path-audit}"
mkdir -p "$OUTPUT"

clang++ -std=c++17 -O2 \
    -I Shared/DSPCore -I Products/R50/Extension -I Products/R50/DSP \
    Tools/r50_signal_path_audit.cpp -o /tmp/r50_signal_path_audit
/tmp/r50_signal_path_audit "$OUTPUT"

#!/bin/bash
#
# test.sh — build & run the offline DSP regression tests (no Xcode needed).
#
set -euo pipefail
cd "$(dirname "$0")/.."
clang++ -std=c++17 -O2 Tests/test_dsp.cpp -o /tmp/analog8_test_dsp
/tmp/analog8_test_dsp

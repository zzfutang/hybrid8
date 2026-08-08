#!/bin/bash
#
# test-r50.sh — build & run the offline R50 regression tests (no Xcode needed).
#
# Runs twice: once optimised, then once under ThreadSanitizer. The second pass
# is what actually verifies the parameter store is race-free — the concurrent
# writer test is only a smoke test without it.
#
set -euo pipefail
cd "$(dirname "$0")/.."

INCLUDES=(-I Shared/DSPCore -I Products/R50/Extension -I Products/R50/DSP)

echo "==> Building tests (-O2)"
clang++ -std=c++17 -O2 "${INCLUDES[@]}" Tests/test_r50.cpp -o /tmp/r50_test_dsp
/tmp/r50_test_dsp

echo
echo "==> Re-running under ThreadSanitizer"
clang++ -std=c++17 -O1 -g -fsanitize=thread "${INCLUDES[@]}" \
  Tests/test_r50.cpp -o /tmp/r50_test_dsp_tsan
TSAN_OPTIONS="halt_on_error=1" /tmp/r50_test_dsp_tsan
echo "==> No data races reported."

#!/bin/bash
#
# audit-r50-dsp-stages.sh — build & run the R50 DSP stage audit: saw sweep
# with spectral measurements at oscillator, post-filter, post-mixer,
# post-volume and post-limiter taps, plus a full-engine chord linearity pass.
#
set -euo pipefail
cd "$(dirname "$0")/.."

INCLUDES=(-I Shared/DSPCore -I Products/R50/Extension -I Products/R50/DSP)

clang++ -std=c++17 -O2 "${INCLUDES[@]}" Tools/r50_dsp_stage_audit.cpp \
  -o /tmp/r50_dsp_stage_audit
/tmp/r50_dsp_stage_audit

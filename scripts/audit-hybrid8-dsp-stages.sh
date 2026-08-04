#!/bin/bash
#
# audit-hybrid8-dsp-stages.sh — build & run the Hybrid8 DSP stage audit: saw
# sweep with spectral measurements at oscillator, post-filter, post-mixer,
# post-softclip and post-volume taps, plus a full-engine chord IMD pass with
# the Analog control off and at its default.
#
set -euo pipefail
cd "$(dirname "$0")/.."

clang++ -std=c++17 -O2 Tools/hybrid8_dsp_stage_audit.cpp \
  -o /tmp/hybrid8_dsp_stage_audit
/tmp/hybrid8_dsp_stage_audit

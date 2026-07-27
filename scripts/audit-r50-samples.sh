#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."

OUTPUT="${1:-build/sample-audit}"
mkdir -p "$OUTPUT"

clang++ -std=c++17 -O2 \
    -I Shared/DSPCore -I Products/R50/DSP \
    Tools/r50_sample_audit.cpp -o /tmp/r50_sample_audit

set +e
/tmp/r50_sample_audit "$OUTPUT" "Products/R50/Samples"
STATUS=$?
set -e

if command -v ffprobe >/dev/null 2>&1; then
    printf 'file,codec,sample_rate,channels,bits_per_sample,duration_s\n' \
        > "$OUTPUT/source-files.csv"
    find Products/R50/Samples -maxdepth 1 -type f \
        \( -iname '*.wav' -o -iname '*.aif' \) -print0 |
    while IFS= read -r -d '' FILE; do
        STREAM="$(ffprobe -v error -select_streams a:0 \
            -show_entries stream=codec_name,sample_rate,channels,bits_per_sample \
            -of csv=p=0 "$FILE")"
        DURATION="$(ffprobe -v error -show_entries format=duration \
            -of default=noprint_wrappers=1:nokey=1 "$FILE")"
        printf '"%s",%s,%s\n' "$FILE" "$STREAM" "$DURATION" \
            >> "$OUTPUT/source-files.csv"
    done
fi

exit "$STATUS"

#!/bin/bash
#
# End-to-end regression check for the glsl_to_usf conversion tool. Requires that
# /tmp/tgfx_dump was populated beforehand by a `TGFX_SHADER_DUMP_DIR=/tmp/tgfx_dump TGFXFullTest`
# run (Phase A/B instrumentation). Verifies that rect-sampler normalization lets the tool
# convert every previously-skipped program and that the manifest records the rect metadata.
#
# Usage: tools/glsl_to_usf/tests/run_e2e.sh [path/to/glsl_to_usf]
#
# Exit non-zero on any regression.

set -euo pipefail

TOOL="${1:-cmake-build-debug/glsl_to_usf}"
if [ ! -x "$TOOL" ]; then
  echo "glsl_to_usf binary not found at $TOOL" >&2
  echo "Build it first: cmake --build cmake-build-debug --target glsl_to_usf" >&2
  exit 2
fi

INPUT="${TGFX_DUMP_DIR:-/tmp/tgfx_dump}"
if [ ! -f "$INPUT/programs.jsonl" ]; then
  echo "Expected dump at $INPUT/programs.jsonl — re-run TGFXFullTest with" >&2
  echo "TGFX_SHADER_DUMP_DIR=$INPUT to populate it." >&2
  exit 2
fi

OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

"$TOOL" --input "$INPUT" --output "$OUT" 2> "$OUT/convert.log"
cat "$OUT/convert.log"

ok=$(grep -oE 'Converted [0-9]+ program' "$OUT/convert.log" | grep -oE '[0-9]+' | head -1)
skipped=$(grep -oE '[0-9]+ skipped' "$OUT/convert.log" | grep -oE '[0-9]+' | head -1)
failed=$(grep -oE '[0-9]+ failed' "$OUT/convert.log" | grep -oE '[0-9]+' | head -1)
echo "ok=$ok skipped=$skipped failed=$failed"

# Phase D target: no rect-induced skips or failures. Baseline of 92 ok before Phase D plus
# ~102 rect programs ⇒ 194 ok minimum.
if [ "${ok:-0}" -lt 194 ]; then
  echo "REGRESSION: expected >= 194 successful conversions, got $ok" >&2
  exit 3
fi
if [ "${failed:-0}" -ne 0 ]; then
  echo "REGRESSION: $failed program(s) failed to convert" >&2
  exit 4
fi

if ! grep -q '"rectSamplers"' "$OUT/manifest.json"; then
  echo "REGRESSION: manifest.json has no rectSamplers entries" >&2
  exit 5
fi

echo "run_e2e.sh: PASS"

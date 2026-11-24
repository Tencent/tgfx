#!/bin/sh
# Captures the GL shader texts emitted by the FP-tree route into a stable hash manifest.
# The dump order is test-order dependent, so the manifest keeps only the sorted content hashes.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BINARY=${TGFX_TEST_BINARY:-"$ROOT/cmake-build-debug/TGFXFullTest_OpenGL"}
FILTER=${TGFX_GOLDEN_FILTER:-"CanvasTest.*:FilterTest.*:ImageRenderTest.*:TextRenderTest.*:RRectTest.*:PathTest.*:LayerFilterTest.*:SurfaceRenderTest.*:VectorLayerTest.*:GLRenderTest.*:LayerTest.*"}
OUT_DIR=${1:-"$ROOT/test/goldens/gl"}
TMP_DIR=$(mktemp -d "${TMPDIR:-/tmp}/tgfx-gl-goldens.XXXXXX")
trap 'rm -rf "$TMP_DIR"' EXIT HUP INT TERM

if [ ! -x "$BINARY" ]; then
  echo "missing test binary: $BINARY" >&2
  exit 2
fi

mkdir -p "$OUT_DIR/samples"
TGFX_DUMP_PROGRAM="$TMP_DIR" "$BINARY" --gtest_filter="$FILTER" >/dev/null 2>&1 || true

FILES=$(find "$TMP_DIR" -type f \( -name '*.vert' -o -name '*.frag' \) | sort)
COUNT=$(printf '%s\n' "$FILES" | sed '/^$/d' | wc -l | tr -d ' ')
if [ "$COUNT" -eq 0 ]; then
  echo "no shader dumps produced; check TGFX_GOLDEN_FILTER and the binary" >&2
  exit 1
fi

hash_file() {
  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$1" | awk '{print $1}'
  else
    sha256sum "$1" | awk '{print $1}'
  fi
}

: >"$OUT_DIR/manifest.sha256"
for file in $FILES; do
  hash_file "$file"
done | sort >"$OUT_DIR/manifest.sha256"

# Keep a few full texts next to the manifest so a mismatch is diagnosable without a rerun.
rm -f "$OUT_DIR"/samples/*.vert "$OUT_DIR"/samples/*.frag
printf '%s\n' "$FILES" | sed '/^$/d' | sed -n '1,6p' | while IFS= read -r file; do
  cp "$file" "$OUT_DIR/samples/$(basename "$file")"
done

echo "captured $COUNT stage dumps from $FILTER"
echo "manifest: $OUT_DIR/manifest.sha256"

#!/bin/sh
# Re-runs the GL shader dump and compares the sorted content hashes with the frozen manifest.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BINARY=${TGFX_TEST_BINARY:-"$ROOT/cmake-build-debug/TGFXFullTest_OpenGL"}
FILTER=${TGFX_GOLDEN_FILTER:-"CanvasTest.*:FilterTest.*:ImageRenderTest.*:TextRenderTest.*:RRectTest.*:PathTest.*:LayerFilterTest.*:SurfaceRenderTest.*:VectorLayerTest.*:GLRenderTest.*:LayerTest.*"}
GOLDEN_DIR=${1:-"$ROOT/test/goldens/gl"}
TMP_DIR=$(mktemp -d "${TMPDIR:-/tmp}/tgfx-gl-check.XXXXXX")
trap 'rm -rf "$TMP_DIR"' EXIT HUP INT TERM

if [ ! -x "$BINARY" ]; then
  echo "missing test binary: $BINARY" >&2
  exit 2
fi
if [ ! -f "$GOLDEN_DIR/manifest.sha256" ]; then
  echo "missing golden manifest: $GOLDEN_DIR/manifest.sha256" >&2
  exit 2
fi

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

for file in $FILES; do
  hash_file "$file"
done | sort >"$TMP_DIR/manifest.sha256"

if ! diff -u "$GOLDEN_DIR/manifest.sha256" "$TMP_DIR/manifest.sha256" >"$TMP_DIR/diff.txt"; then
  echo "GL shader golden mismatch:" >&2
  cat "$TMP_DIR/diff.txt" >&2
  echo "captured $COUNT stage dumps; inspect $TMP_DIR before it is removed" >&2
  trap - EXIT
  exit 1
fi

echo "GL shader golden match: $COUNT stage dumps"

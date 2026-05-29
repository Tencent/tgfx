#!/bin/bash

# Accept screenshot baseline changes.
# Prerequisites: Run TGFXFullTest_{Backend} first, confirm screenshots in test/out/ are correct,
# then run this script to accept.
#
# This script automatically runs UpdateBaseline for all backends available on the current platform:
#   macOS:   OpenGL
#   Windows: Vulkan
#
# No arguments required.

set -e
cd "$(dirname "$0")"

if [ ! -f "test/out/version.json" ]; then
  echo "Error: test/out/version.json not found."
  echo "Please run TGFXFullTest_{Backend} first, confirm the screenshots, then run this script."
  exit 1
fi

echo "Step 1: Accepting version.json..."
cp test/out/version.json test/baseline/version.json

# Determine which backends to refresh based on current platform.
OS=$(uname -s)
case "$OS" in
  Darwin)
    BACKENDS=("OpenGL:")
    ;;
  MINGW*|MSYS*|CYGWIN*|Windows_NT)
    BACKENDS=("Vulkan:-DTGFX_USE_VULKAN=ON")
    ;;
  *)
    BACKENDS=("OpenGL:")
    ;;
esac

echo "Step 2: Refreshing .cache for all local backends..."

for entry in "${BACKENDS[@]}"; do
  TARGET_SUFFIX="${entry%%:*}"
  CMAKE_ARGS="${entry#*:}"
  BUILD_DIR="cmake-build-accept-baseline-${TARGET_SUFFIX}"

  echo ""
  echo "--- UpdateBaseline_${TARGET_SUFFIX} (${CMAKE_ARGS:-default}) ---"

  # Clean stale build directory from previous interrupted runs.
  rm -rf "$BUILD_DIR"

  cmake -G Ninja $CMAKE_ARGS -DTGFX_BUILD_TESTS=ON -DTGFX_SKIP_BASELINE_CHECK=ON \
        -DCMAKE_BUILD_TYPE=Debug -B "$BUILD_DIR"
  cmake --build "$BUILD_DIR" --target UpdateBaseline_${TARGET_SUFFIX}
  ./"$BUILD_DIR"/UpdateBaseline_${TARGET_SUFFIX}

  echo "--- UpdateBaseline_${TARGET_SUFFIX} done ---"
done

echo ""
echo "Step 3: Syncing version.json to web baseline caches (if they exist)..."
for WEB_BACKEND in webgl webgpu; do
  WEB_CACHE="test/baseline/.cache/${WEB_BACKEND}"
  if [ -d "$WEB_CACHE" ]; then
    cp test/baseline/version.json "$WEB_CACHE/version.json"
    echo "  Updated $WEB_CACHE/version.json"
  fi
done

echo ""
echo "Baseline accepted for all local backends. Commit:"
echo "  git add test/baseline/version.json && git commit -m 'Update baseline.'"

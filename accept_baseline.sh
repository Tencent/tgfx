#!/bin/bash

# Accept screenshot baseline changes.
# Prerequisites: Run TGFXFullTest_{Backend} first, confirm screenshots in test/out/ are correct,
# then run this script to accept.
#
# Usage:
#   ./accept_baseline.sh USE_OPENGL_SWIFTSHADER
#   ./accept_baseline.sh USE_OPENGL
#   ./accept_baseline.sh USE_VULKAN_SWIFTSHADER
#   ./accept_baseline.sh USE_VULKAN
#   ./accept_baseline.sh USE_METAL

set -e

if [ ! -f "test/out/version.json" ]; then
  echo "Error: test/out/version.json not found."
  echo "Please run TGFXFullTest_{Backend} first, confirm the screenshots, then run this script."
  exit 1
fi

# Determine cmake args and target suffix
case "$1" in
  USE_OPENGL_SWIFTSHADER)
    CMAKE_ARGS="-DTGFX_USE_SWIFTSHADER=ON"
    TARGET_SUFFIX="OpenGL" ;;
  USE_VULKAN_SWIFTSHADER)
    CMAKE_ARGS="-DTGFX_USE_VULKAN=ON -DTGFX_USE_SWIFTSHADER=ON"
    TARGET_SUFFIX="Vulkan" ;;
  USE_VULKAN)
    CMAKE_ARGS="-DTGFX_USE_VULKAN=ON"
    TARGET_SUFFIX="Vulkan" ;;
  USE_METAL)
    CMAKE_ARGS="-DTGFX_USE_METAL=ON"
    TARGET_SUFFIX="Metal" ;;
  USE_OPENGL)
    CMAKE_ARGS=""
    TARGET_SUFFIX="OpenGL" ;;
  *)
    echo "Error: Unsupported backend argument: $1"
    echo "Supported: USE_OPENGL_SWIFTSHADER, USE_OPENGL, USE_VULKAN_SWIFTSHADER, USE_VULKAN, USE_METAL"
    exit 1 ;;
esac

echo "Step 1: Building and running UpdateBaseline_${TARGET_SUFFIX} to refresh .cache..."
cmake -G Ninja $CMAKE_ARGS -DTGFX_BUILD_TESTS=ON -DTGFX_SKIP_BASELINE_CHECK=ON \
      -DCMAKE_BUILD_TYPE=Debug -B cmake-build-debug
cmake --build cmake-build-debug --target UpdateBaseline_${TARGET_SUFFIX}
./cmake-build-debug/UpdateBaseline_${TARGET_SUFFIX}

echo "Step 2: Accepting version.json..."
cp test/out/version.json test/baseline/version.json

echo "Baseline accepted. Commit:"
echo "  git add test/baseline/version.json && git commit -m 'Update baseline.'"

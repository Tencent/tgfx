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

echo "Step 1: Merging version.json (update existing keys, add new keys, preserve others)..."

# Merge test/out/version.json INTO test/baseline/version.json:
#   - Keys present in out: update with new value
#   - Keys only in baseline: preserve as-is
#   - Keys only in out: add to baseline
python3 -c "
import json

baseline_path = 'test/baseline/version.json'
out_path = 'test/out/version.json'

with open(baseline_path, 'r') as f:
    baseline = json.load(f)
with open(out_path, 'r') as f:
    out = json.load(f)

# Deep merge: out overwrites baseline at the key level
for category, keys in out.items():
    if category not in baseline:
        baseline[category] = {}
    for key, value in keys.items():
        baseline[category][key] = value

# Sort for deterministic output
sorted_baseline = {}
for category in sorted(baseline.keys()):
    sorted_baseline[category] = dict(sorted(baseline[category].items()))

with open(baseline_path, 'w') as f:
    json.dump(sorted_baseline, f, indent=4)
    f.write('\n')

print(f'  Merged {sum(len(v) for v in out.values())} keys from out into baseline.')
print(f'  Baseline now has {sum(len(v) for v in sorted_baseline.values())} total keys.')
"

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

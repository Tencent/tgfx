#!/bin/bash

# Update web baseline cache by running tests on main branch.
#
# This generates the baseline cache (md5.json + version.json) from main into
# test/baseline/.cache-web/, which subsequent test runs on feature branches
# compare against.
#
# After running this script, use `npm run autotest` on your feature branch
# to verify against the main baseline.
#
# Usage: cd web/test && bash update_baseline.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
WEB_TEST_DIR="$SCRIPT_DIR"
BUILD_DIR="$WEB_TEST_DIR/build"

cd "$PROJECT_ROOT"

# Record current branch to return later.
CURRENT_BRANCH=$(git branch --show-current)
if [ -z "$CURRENT_BRANCH" ]; then
  CURRENT_BRANCH=$(git rev-parse HEAD)
fi

echo "Current branch: $CURRENT_BRANCH"

# Check for uncommitted changes.
HAS_CHANGES=0
if ! git diff --quiet || ! git diff --cached --quiet; then
  HAS_CHANGES=1
fi

if [ "$HAS_CHANGES" -eq 1 ]; then
  echo "Stashing uncommitted changes..."
  git stash push -m "update_baseline_web: auto stash"
fi

echo ""
echo "Switching to main..."
git checkout main
git pull --ff-only origin main 2>/dev/null || true

echo ""
echo "Building web tests on main (UPDATE_BASELINE mode)..."
cd "$WEB_TEST_DIR"
rm -rf "$BUILD_DIR"
emcmake cmake -B build -DCMAKE_BUILD_TYPE=Release -DTGFX_UPDATE_BASELINE=ON
cmake --build build -- -j 8
npm run copy

echo ""
echo "Running web tests on main..."
node run_test.js || true

echo ""
echo "Switching back to $CURRENT_BRANCH..."
cd "$PROJECT_ROOT"
git checkout "$CURRENT_BRANCH"

if [ "$HAS_CHANGES" -eq 1 ]; then
  echo "Restoring stashed changes..."
  git stash pop
fi

echo ""
echo "Baseline cache updated at: test/baseline/.cache-web/"
echo "Run 'npm run autotest' to verify your branch against the baseline."

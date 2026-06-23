#!/bin/bash

# Internal script for updating web baseline cache. DO NOT call directly.
# Use the project root entry point instead:
#   ./update_baseline.sh USE_WEBGL
#   ./update_baseline.sh USE_WEBGPU
#
# This script is invoked by the root update_baseline.sh with a backend argument.

set -e

# Guard against direct invocation.
if [ -z "$TGFX_WEB_BASELINE_INVOKED" ]; then
    echo "Error: Do not run this script directly."
    echo "Use from project root:"
    echo "  ./update_baseline.sh USE_WEBGL"
    echo "  ./update_baseline.sh USE_WEBGPU"
    exit 1
fi

BACKEND=${1:-webgl}

if [ "$BACKEND" != "webgl" ] && [ "$BACKEND" != "webgpu" ]; then
    echo "Error: Invalid backend '$BACKEND'. Must be 'webgl' or 'webgpu'."
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Auto-detect emsdk if not already active.
if [ -z "$EMSDK" ]; then
    source "$SCRIPT_DIR/../setup_emsdk.sh"
fi

PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
WEB_TEST_DIR="$SCRIPT_DIR"
BUILD_DIR="$WEB_TEST_DIR/build-${BACKEND}"
CACHE_VERSION_FILE="$PROJECT_ROOT/test/baseline/.cache/${BACKEND}/version.json"

cd "$PROJECT_ROOT"

# Check if cache is up to date with origin/main.
if [ -f "$CACHE_VERSION_FILE" ]; then
    MAIN_VERSION=$(git show origin/main:test/baseline/version.json 2>/dev/null)
    if [ -n "$MAIN_VERSION" ]; then
        CACHE_CONTENT=$(cat "$CACHE_VERSION_FILE")
        if [ "$MAIN_VERSION" = "$CACHE_CONTENT" ]; then
            exit 0
        fi
    fi
fi

echo "~~~~~~~~~~~~~~~~~~~Update Baseline ($BACKEND) Start~~~~~~~~~~~~~~~~~~~~~"
CURRENT_BRANCH=$(git rev-parse --abbrev-ref HEAD)
CURRENT_COMMIT=$(git rev-parse HEAD)
# Remove build artifacts before stash to avoid "already exists" conflicts on pop.
rm -rf "$BUILD_DIR"
STASH_LIST_BEFORE=$(git stash list)
git stash push --include-untracked --quiet
STASH_LIST_AFTER=$(git stash list)

restore_branch() {
    cd "$PROJECT_ROOT"
    if [[ $CURRENT_BRANCH == "HEAD" ]]; then
        git checkout "$CURRENT_COMMIT" --quiet 2>/dev/null || true
    else
        git switch "$CURRENT_BRANCH" --quiet 2>/dev/null || true
    fi
    if [[ $STASH_LIST_BEFORE != "$STASH_LIST_AFTER" ]]; then
        git stash pop --index --quiet 2>/dev/null || true
    fi
}
trap restore_branch EXIT

git switch main --quiet

cd "$WEB_TEST_DIR"

echo ""
echo "Building web tests on main (UPDATE_BASELINE mode, backend: $BACKEND)..."
rm -rf "$BUILD_DIR"

CMAKE_ARGS="-DTGFX_UPDATE_BASELINE=ON"
if [ "$BACKEND" = "webgpu" ]; then
    CMAKE_ARGS="$CMAKE_ARGS -DTGFX_USE_WEBGPU=ON"
fi

emcmake cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release $CMAKE_ARGS
cmake --build "$BUILD_DIR" -- -j 8
npm run "copy:${BACKEND}"

echo ""
echo "Running web tests on main..."
node run_test.js --backend="$BACKEND" || true

cd "$PROJECT_ROOT"

echo "~~~~~~~~~~~~~~~~~~~Update Baseline ($BACKEND) Success~~~~~~~~~~~~~~~~~~~~~"
echo "Baseline cache updated at: test/baseline/.cache/${BACKEND}/"

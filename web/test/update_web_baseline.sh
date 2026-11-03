#!/bin/bash

# Internal main reference generator. Use ./update_baseline.sh USE_WEBGL or USE_WEBGPU.
# This does not accept changes from the current branch or modify the tracked baseline version.
set -eo pipefail

if [ -z "${TGFX_WEB_BASELINE_INVOKED:-}" ]; then
    echo "Error: Use ./update_baseline.sh USE_WEBGL or USE_WEBGPU from the project root."
    exit 1
fi

BACKEND=${1:-webgl}
if [ "$BACKEND" != "webgl" ] && [ "$BACKEND" != "webgpu" ]; then
    echo "Error: Invalid backend '$BACKEND'."
    exit 1
fi
SKIP_IMAGES=OFF
if [ "${2:-}" = "--skip-images" ]; then
    SKIP_IMAGES=ON
elif [ -n "${2:-}" ]; then
    echo "Error: Unknown option '$2'."
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$PROJECT_ROOT"
REFERENCE_COMMIT=$(git rev-parse --verify 'origin/main^{commit}')
if node "$SCRIPT_DIR/reference_cache.js" --check "$BACKEND" "$REFERENCE_COMMIT"; then
    echo "Complete $BACKEND reference cache already matches origin/main."
    exit 0
fi

# Reuse installed dependencies only when their definitions match the pinned source.
if ! git diff --quiet "$REFERENCE_COMMIT" -- DEPS web/package.json web/package-lock.json; then
    echo "Error: Dependency definitions differ from origin/main. Install matching dependencies first."
    exit 1
fi
if [ ! -d "$PROJECT_ROOT/third_party/vendor_tools" ] || [ ! -f "$PROJECT_ROOT/web/node_modules/rollup/dist/bin/rollup" ]; then
    echo "Error: Installed C++ dependencies and Web build dependencies are required."
    exit 1
fi
if [ -z "${EMSDK:-}" ]; then
    source "$SCRIPT_DIR/../setup_emsdk.sh"
fi

# Archive a fixed commit instead of changing or stashing the user's worktree.
mkdir -p "$SCRIPT_DIR/build-reference-$BACKEND"
SESSION=$(mktemp -d "$SCRIPT_DIR/build-reference-$BACKEND/run.XXXXXX")
SOURCE_DIR="$SESSION/source"
BUILD_DIR="$SESSION/build"
RUNTIME_DIR="$SESSION/runtime"
mkdir -p "$SOURCE_DIR" "$RUNTIME_DIR"
echo "Generating $BACKEND reference from origin/main ${REFERENCE_COMMIT:0:9} in $SESSION"
git archive "$REFERENCE_COMMIT" | tar -x -C "$SOURCE_DIR"
node "$SCRIPT_DIR/reference_source.js" "$SOURCE_DIR"
ln -s "$PROJECT_ROOT/third_party" "$SOURCE_DIR/third_party"
ln -s "$PROJECT_ROOT/web/node_modules" "$SOURCE_DIR/web/node_modules"
(
    cd "$SOURCE_DIR/web"
    node node_modules/rollup/dist/bin/rollup -c script/rollup.tgfx.js
)

USE_WEBGPU=OFF
if [ "$BACKEND" = "webgpu" ]; then
    USE_WEBGPU=ON
fi
emcmake cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DTGFX_SOURCE_DIR="$SOURCE_DIR" \
    -DTGFX_REFERENCE_COMMIT="$REFERENCE_COMMIT" \
    -DTGFX_UPDATE_BASELINE=ON \
    -DTGFX_USE_WEBGPU="$USE_WEBGPU" \
    -DTGFX_SKIP_GENERATE_BASELINE_IMAGES="$SKIP_IMAGES"
cmake --build "$BUILD_DIR" --parallel 8
cp "$BUILD_DIR/tgfx-test.js" "$BUILD_DIR/tgfx-test.wasm" "$BUILD_DIR/tgfx-test.data" "$RUNTIME_DIR/"
cp "$SCRIPT_DIR/index.html" "$RUNTIME_DIR/"
cp "$SOURCE_DIR/web/lib/tgfx.esm.js" "$RUNTIME_DIR/"

node "$SCRIPT_DIR/run_test.js" --backend="$BACKEND" --mode=reference \
    --reference-commit="$REFERENCE_COMMIT" --web-root="$RUNTIME_DIR"
node "$SCRIPT_DIR/reference_cache.js" --check "$BACKEND" "$REFERENCE_COMMIT"
echo "Reference cache prepared at test/baseline/.cache/$BACKEND/"
echo "Generation artifacts retained at $SESSION"

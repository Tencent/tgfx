#!/usr/bin/env bash

# Update local baseline cache from remote changes.
# Run this after pulling main branch that contains baseline changes from others.
# Without updating the cache, affected tests will be skipped, leading to inaccurate results.
#
# Usage:
#   ./update_baseline.sh [<BACKEND>] [--skip-images]
#
# BACKEND (order-insensitive with --skip-images):
#   USE_OPENGL                 OpenGL (real GPU)
#   USE_OPENGL_SWIFTSHADER     OpenGL (SwiftShader)
#   USE_VULKAN                 Vulkan (real GPU)
#   USE_VULKAN_SWIFTSHADER     Vulkan (SwiftShader)
#   USE_METAL                  Metal (Apple GPU)
#   USE_WEBGL                  Web (WebGL)
#   USE_WEBGPU                 Web (WebGPU)
#
# Options:
#   --skip-images   Skip writing baseline `_base.webp` images. The cache
#                   (md5.json + version.json) is still refreshed, which is
#                   all the CI pipelines need. Local runs omit this to also
#                   produce baseline-out/ images for visual inspection.

{
  # Parse arguments: separate the backend keyword from the --skip-images flag so callers may
  # pass them in any order (e.g. `./update_baseline.sh --skip-images USE_METAL`).
  BACKEND_ARG=""
  SKIP_IMAGES=0
  for arg in "$@"; do
    case "$arg" in
      --skip-images) SKIP_IMAGES=1 ;;
      *) BACKEND_ARG="$arg" ;;
    esac
  done

  # Web backends are handled by a separate script with Emscripten toolchain. Forward the
  # --skip-images flag so `./update_baseline.sh USE_WEBGL --skip-images` really does skip
  # emitting `_base.webp` images on the web side (aligned with native backends).
  WEB_SKIP_FLAG=""
  if [ "$SKIP_IMAGES" = "1" ]; then
    WEB_SKIP_FLAG="--skip-images"
  fi
  case "$BACKEND_ARG" in
    USE_WEBGL)
      TGFX_WEB_BASELINE_INVOKED=1 exec bash web/test/update_web_baseline.sh webgl $WEB_SKIP_FLAG
      ;;
    USE_WEBGPU)
      TGFX_WEB_BASELINE_INVOKED=1 exec bash web/test/update_web_baseline.sh webgpu $WEB_SKIP_FLAG
      ;;
  esac

  # Determine cmake args and backend name
  case "$BACKEND_ARG" in
    USE_OPENGL_SWIFTSHADER)
      CMAKE_BACKEND_ARGS="-DTGFX_USE_SWIFTSHADER=ON"
      BACKEND_NAME="opengl-swiftshader" ;;
    USE_VULKAN_SWIFTSHADER)
      CMAKE_BACKEND_ARGS="-DTGFX_USE_VULKAN=ON -DTGFX_USE_SWIFTSHADER=ON"
      BACKEND_NAME="vulkan-swiftshader" ;;
    USE_VULKAN)
      CMAKE_BACKEND_ARGS="-DTGFX_USE_VULKAN=ON"
      BACKEND_NAME="vulkan" ;;
    USE_METAL)
      CMAKE_BACKEND_ARGS="-DTGFX_USE_METAL=ON"
      BACKEND_NAME="metal" ;;
    *)
      CMAKE_BACKEND_ARGS=""
      BACKEND_NAME="opengl" ;;
  esac

  CACHE_VERSION_FILE=./test/baseline/.cache/$BACKEND_NAME/version.json

  # Check if cache is up to date with origin/main
  if [ -f "$CACHE_VERSION_FILE" ]; then
    MAIN_VERSION=$(git show origin/main:test/baseline/version.json 2>/dev/null)
    if [ -n "$MAIN_VERSION" ]; then
      CACHE_CONTENT=$(cat "$CACHE_VERSION_FILE")
      if [ "$MAIN_VERSION" = "$CACHE_CONTENT" ]; then
        exit 0
      fi
    fi
  fi

  echo "~~~~~~~~~~~~~~~~~~~Update Baseline ($BACKEND_NAME) Start~~~~~~~~~~~~~~~~~~~~~"
  CURRENT_BRANCH=$(git rev-parse --abbrev-ref HEAD)
  CURRENT_COMMIT=$(git rev-parse HEAD)
  # Remove build artifacts before stash to avoid "already exists" conflicts on pop.
  rm -rf build-update-baseline
  STASH_LIST_BEFORE=$(git stash list)
  git stash push --include-untracked --quiet
  STASH_LIST_AFTER=$(git stash list)

  # Detach at the baseline ref instead of switching to the local main branch: `git switch main`
  # fails when main is already checked out in another worktree, and that failure used to go
  # unnoticed, so the baseline got generated from the current branch and every comparison became
  # self-referential (all screenshot/PDF tests pass locally while CI fails). Prefer origin/main so
  # the generated cache matches the version.json used by the up-to-date check above.
  BASELINE_REF=origin/main
  if ! git rev-parse --verify --quiet "$BASELINE_REF" >/dev/null; then
    BASELINE_REF=main
  fi
  if ! git switch --detach "$BASELINE_REF" --quiet; then
    echo "~~~~~~~~~~~~~~~~~~~Update Baseline ($BACKEND_NAME) Failed: cannot check out $BASELINE_REF~~~~~~~~~~~~~~~~~~"
    if [[ $STASH_LIST_BEFORE != "$STASH_LIST_AFTER" ]]; then
      git stash pop --index --quiet
    fi
    exit 1
  fi

  ./install_tools.sh
  depsync

  BUILD_DIR=build-update-baseline

  rm -rf ${BUILD_DIR}
  mkdir ${BUILD_DIR}
  cd ${BUILD_DIR}

  # Determine target suffix
  case "$BACKEND_ARG" in
    USE_VULKAN_SWIFTSHADER|USE_VULKAN)
      TARGET_SUFFIX="Vulkan" ;;
    USE_METAL)
      TARGET_SUFFIX="Metal" ;;
    *)
      TARGET_SUFFIX="OpenGL" ;;
  esac

  # Default to generating baseline images so local users get baseline-out/ for inspection.
  # CI pipelines pass --skip-images because they only need the cache (md5.json + version.json).
  SKIP_IMAGES_FLAG=""
  if [ "$SKIP_IMAGES" = "1" ]; then
    SKIP_IMAGES_FLAG="-DTGFX_SKIP_GENERATE_BASELINE_IMAGES=ON"
  fi

  cmake $CMAKE_BACKEND_ARGS $SKIP_IMAGES_FLAG \
        -DTGFX_BUILD_TESTS=ON -DTGFX_SKIP_BASELINE_CHECK=ON \
        -DCMAKE_BUILD_TYPE=Debug ../

  cmake --build . --target UpdateBaseline_${TARGET_SUFFIX} -- -j 12

  # Set up SwiftShader Vulkan library so volk can load it at runtime.
  if [[ "$BACKEND_ARG" == "USE_VULKAN_SWIFTSHADER" ]]; then
    HOST_ARCH=$(uname -m)
    if [[ "$HOST_ARCH" == "x86_64" ]]; then
      HOST_ARCH=x64
    fi
    cp "../vendor/swiftshader/mac/$HOST_ARCH/libvk_swiftshader.dylib" "$(pwd)/libvulkan.dylib"
    export DYLD_LIBRARY_PATH="$(pwd):${DYLD_LIBRARY_PATH:-}"
  fi

  ./UpdateBaseline_${TARGET_SUFFIX}

  if test $? -eq 0; then
     echo "~~~~~~~~~~~~~~~~~~~Update Baseline ($BACKEND_NAME) Success~~~~~~~~~~~~~~~~~~~~~"
  else
    echo "~~~~~~~~~~~~~~~~~~~Update Baseline ($BACKEND_NAME) Failed~~~~~~~~~~~~~~~~~~"
    COMPLIE_RESULT=false
  fi

  cd ..

  if [[ $CURRENT_BRANCH == "HEAD" ]]; then
      git checkout $CURRENT_COMMIT --quiet
  else
      git switch $CURRENT_BRANCH --quiet
  fi
  if [[ $STASH_LIST_BEFORE != "$STASH_LIST_AFTER" ]]; then
    git stash pop --index --quiet
  fi

  depsync

  if [ "$COMPLIE_RESULT" == false ]; then
    mkdir -p result
    # Copy test output for CI diagnostic upload (if it exists).
    # UpdateBaseline may not produce test/out/ — it writes to .cache/ instead.
    if [ -d test/out ]; then
      cp -r test/out result
    fi
    exit 1
  fi
  rm -rf ${BUILD_DIR}
}

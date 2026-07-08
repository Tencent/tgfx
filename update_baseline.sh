#!/usr/bin/env bash

# Update local baseline cache from remote changes.
# Run this after pulling main branch that contains baseline changes from others.
# Without updating the cache, affected tests will be skipped, leading to inaccurate results.
#
# Usage:
#   ./update_baseline.sh USE_OPENGL                 # OpenGL (真机 GPU)
#   ./update_baseline.sh USE_OPENGL_SWIFTSHADER     # OpenGL (SwiftShader)
#   ./update_baseline.sh USE_VULKAN                 # Vulkan (真机 GPU)
#   ./update_baseline.sh USE_VULKAN_SWIFTSHADER     # Vulkan (SwiftShader)
#   ./update_baseline.sh USE_METAL                  # Metal (Apple GPU)
#   ./update_baseline.sh USE_WEBGL                  # Web (WebGL)
#   ./update_baseline.sh USE_WEBGPU                 # Web (WebGPU)

{
  # Web backends are handled by a separate script with Emscripten toolchain.
  case "$1" in
    USE_WEBGL)
      TGFX_WEB_BASELINE_INVOKED=1 exec bash web/test/update_web_baseline.sh webgl
      ;;
    USE_WEBGPU)
      TGFX_WEB_BASELINE_INVOKED=1 exec bash web/test/update_web_baseline.sh webgpu
      ;;
  esac

  # Determine cmake args and backend name
  case "$1" in
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
  git switch main --quiet

  ./install_tools.sh
  depsync

  BUILD_DIR=build-update-baseline

  rm -rf ${BUILD_DIR}
  mkdir ${BUILD_DIR}
  cd ${BUILD_DIR}

  # Determine target suffix
  case "$1" in
    USE_VULKAN_SWIFTSHADER|USE_VULKAN)
      TARGET_SUFFIX="Vulkan" ;;
    USE_METAL)
      TARGET_SUFFIX="Metal" ;;
    *)
      TARGET_SUFFIX="OpenGL" ;;
  esac

  cmake $CMAKE_BACKEND_ARGS -DTGFX_SKIP_GENERATE_BASELINE_IMAGES=ON \
        -DTGFX_BUILD_TESTS=ON -DTGFX_SKIP_BASELINE_CHECK=ON \
        -DCMAKE_BUILD_TYPE=Debug ../

  cmake --build . --target UpdateBaseline_${TARGET_SUFFIX} -- -j 12

  # Set up SwiftShader Vulkan library so volk can load it at runtime.
  if [[ "$1" == "USE_VULKAN_SWIFTSHADER" ]]; then
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

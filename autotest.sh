#!/usr/bin/env bash

function make_dir() {
  rm -rf $1
  mkdir -p $1
}
echo "shell log - autotest start"
./install_tools.sh

echo $(pwd)

COMPLIE_RESULT=true

WORKSPACE=$(pwd)

cd $WORKSPACE

# Parse arguments the same way update_baseline.sh does, so the backend keyword and the
# --skip-images flag can appear in any order (e.g. `./autotest.sh --skip-images USE_METAL`).
# Without this, the dispatch below would reject --skip-images as an unknown backend even
# though update_baseline.sh (invoked next) handles it fine, leaving the user in a confusing
# half-executed state.
BACKEND_ARG=""
for arg in "$@"; do
  case "$arg" in
    --skip-images) ;;
    *) BACKEND_ARG="$arg" ;;
  esac
done

./update_baseline.sh "$@"
if test $? -ne 0; then
   exit 1
fi

make_dir result
make_dir build
cd build

# Determine cmake args and target suffix
if [[ "$BACKEND_ARG" == "USE_OPENGL_SWIFTSHADER" ]]; then
  TARGET_SUFFIX="OpenGL"
  cmake -DCMAKE_CXX_FLAGS="-fprofile-arcs -ftest-coverage -g -O0" -DTGFX_USE_SWIFTSHADER=ON -DTGFX_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug ../
elif [[ "$BACKEND_ARG" == "USE_VULKAN_SWIFTSHADER" ]]; then
  TARGET_SUFFIX="Vulkan"
  cmake -DCMAKE_CXX_FLAGS="-fprofile-arcs -ftest-coverage -g -O0" -DTGFX_USE_VULKAN=ON -DTGFX_USE_SWIFTSHADER=ON -DTGFX_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug ../
elif [[ "$BACKEND_ARG" == "USE_METAL" ]]; then
  TARGET_SUFFIX="Metal"
  cmake -DTGFX_USE_METAL=ON -DTGFX_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug ../
elif [[ "$BACKEND_ARG" == "USE_VULKAN" ]]; then
  echo "ERROR: USE_VULKAN (native GPU) is not supported on macOS. Use USE_VULKAN_SWIFTSHADER instead."
  exit 1
elif [[ "$BACKEND_ARG" == "USE_OPENGL" ]]; then
  TARGET_SUFFIX="OpenGL"
  cmake -DCMAKE_CXX_FLAGS="-fprofile-arcs -ftest-coverage -g -O0" -DTGFX_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug ../
else
  echo "ERROR: Unsupported backend argument: $BACKEND_ARG"
  echo "Supported: USE_OPENGL, USE_OPENGL_SWIFTSHADER, USE_VULKAN, USE_VULKAN_SWIFTSHADER, USE_METAL"
  exit 1
fi

if test $? -eq 0; then
  echo "~~~~~~~~~~~~~~~~~~~CMakeLists OK~~~~~~~~~~~~~~~~~~"
else
  echo "~~~~~~~~~~~~~~~~~~~CMakeLists error~~~~~~~~~~~~~~~~~~"
  exit
fi

cmake --build . --target TGFXFullTest_${TARGET_SUFFIX} -- -j 12
if test $? -eq 0; then
  echo "~~~~~~~~~~~~~~~~~~~TGFXFullTest_${TARGET_SUFFIX} make successed~~~~~~~~~~~~~~~~~~"
else
  echo "~~~~~~~~~~~~~~~~~~~TGFXFullTest_${TARGET_SUFFIX} make error~~~~~~~~~~~~~~~~~~"
  exit 1
fi

if [[ "$BACKEND_ARG" == "USE_VULKAN_SWIFTSHADER" ]]; then
  HOST_ARCH=$(uname -m)
  if [[ "$HOST_ARCH" == "x86_64" ]]; then
    HOST_ARCH=x64
  fi
  # Rename SwiftShader to libvulkan.dylib so volk loads it directly (volk uses
  # dlopen("libvulkan.dylib") on macOS, LoadLibrary("vulkan-1.dll") on Windows).
  cp "$WORKSPACE/vendor/swiftshader/mac/$HOST_ARCH/libvk_swiftshader.dylib" "$WORKSPACE/build/libvulkan.dylib"
  export DYLD_LIBRARY_PATH="$WORKSPACE/build:${DYLD_LIBRARY_PATH:-}"
fi

./TGFXFullTest_${TARGET_SUFFIX} --gtest_output=json:TGFXFullTest.json

if test $? -eq 0; then
  echo "~~~~~~~~~~~~~~~~~~~TGFXFullTest_${TARGET_SUFFIX} successed~~~~~~~~~~~~~~~~~~"
else
  echo "~~~~~~~~~~~~~~~~~~~TGFXFullTest_${TARGET_SUFFIX} Failed~~~~~~~~~~~~~~~~~~"
  COMPLIE_RESULT=false
fi

cp -a $WORKSPACE/build/*.json $WORKSPACE/result/

cd ..

cp -r $WORKSPACE/test/out $WORKSPACE/result

if [ "$COMPLIE_RESULT" == false ]; then
  exit 1
fi

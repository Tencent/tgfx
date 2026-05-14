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

./update_baseline.sh "${1:-""}"
if test $? -ne 0; then
   exit 1
fi
cp -r $WORKSPACE/test/baseline $WORKSPACE/result

make_dir result
make_dir build
cd build

if [[ "$1" == "USE_SWIFTSHADER" ]]; then
  cmake -DCMAKE_CXX_FLAGS="-fprofile-arcs -ftest-coverage -g -O0" -DTGFX_USE_SWIFTSHADER=ON -DTGFX_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug ../
elif [[ "$1" == "USE_VULKAN_SWIFTSHADER" ]]; then
  cmake -DCMAKE_CXX_FLAGS="-fprofile-arcs -ftest-coverage -g -O0" -DTGFX_USE_VULKAN=ON -DTGFX_USE_SWIFTSHADER=ON -DTGFX_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug ../
elif [[ "$1" == "USE_METAL" ]]; then
  cmake -DTGFX_USE_METAL=ON -DTGFX_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug ../
else
  cmake -DCMAKE_CXX_FLAGS="-fprofile-arcs -ftest-coverage -g -O0" -DTGFX_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug ../
fi
if test $? -eq 0; then
  echo "~~~~~~~~~~~~~~~~~~~CMakeLists OK~~~~~~~~~~~~~~~~~~"
else
  echo "~~~~~~~~~~~~~~~~~~~CMakeLists error~~~~~~~~~~~~~~~~~~"
  exit
fi

cmake --build . --target TGFXFullTest -- -j 12
if test $? -eq 0; then
  echo "~~~~~~~~~~~~~~~~~~~TGFXFullTest make successed~~~~~~~~~~~~~~~~~~"
else
  echo "~~~~~~~~~~~~~~~~~~~TGFXFullTest make error~~~~~~~~~~~~~~~~~~"
  exit 1
fi

if [[ "$1" == "USE_VULKAN_SWIFTSHADER" ]]; then
  # macOS: SwiftShader ships libvk_swiftshader.dylib, but the Vulkan loader looks for libvulkan.dylib.
  # Copy and rename it next to the test binary, then point DYLD_LIBRARY_PATH there for this process only.
  HOST_ARCH=$(uname -m)
  if [[ "$HOST_ARCH" == "x86_64" ]]; then
    HOST_ARCH=x64
  fi
  cp $WORKSPACE/vendor/swiftshader/mac/$HOST_ARCH/libvk_swiftshader.dylib $WORKSPACE/build/libvulkan.dylib
  export DYLD_LIBRARY_PATH=$WORKSPACE/build:${DYLD_LIBRARY_PATH:-}
fi

./TGFXFullTest --gtest_output=json:TGFXFullTest.json

if test $? -eq 0; then
  echo "~~~~~~~~~~~~~~~~~~~TGFXFullTest successed~~~~~~~~~~~~~~~~~~"
else
  echo "~~~~~~~~~~~~~~~~~~~TGFXFullTest Failed~~~~~~~~~~~~~~~~~~"
  COMPLIE_RESULT=false
fi

cp -a $WORKSPACE/build/*.json $WORKSPACE/result/

cd ..

cp -r $WORKSPACE/test/out $WORKSPACE/result

if [ "$COMPLIE_RESULT" == false ]; then
  exit 1
fi

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

make_dir result
make_dir build

./update_baseline.sh 1
if test $? -ne 0; then
   exit 1
fi
cp -r $WORKSPACE/test/baseline $WORKSPACE/result

cd build

cmake -DCMAKE_CXX_FLAGS="-fprofile-arcs -ftest-coverage -g -O0" -DTGFX_USE_SWIFTSHADER=ON -DTGFX_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug ../
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

./TGFXFullTest --gtest_output=json:TGFXFullTest.json

if test $? -eq 0; then
  echo "~~~~~~~~~~~~~~~~~~~TGFXFullTest successed~~~~~~~~~~~~~~~~~~"
else
  echo "~~~~~~~~~~~~~~~~~~~TGFXFullTest Failed~~~~~~~~~~~~~~~~~~"
  COMPLIE_RESULT=false
fi

cp -a $WORKSPACE/build/*.json $WORKSPACE/result/

cd ..

rm -rf build

cp -r $WORKSPACE/test/out $WORKSPACE/result

if [ "$COMPLIE_RESULT" == false ]; then
  exit 1
fi

#!/usr/bin/env bash

function make_dir() {
  rm -rf $1
  mkdir -p $1
}

echo "开始自动测试..."
./install_tools.sh

WORKSPACE=$(pwd)

# 更新基线
./update_baseline.sh 1
if test $? -ne 0; then
   echo "更新基线失败。"
   exit 1
fi

# 准备目录
make_dir result
make_dir build
cd build

# 配置 CMake
cmake -DCMAKE_CXX_FLAGS="-fprofile-arcs -ftest-coverage -g -O0" \
      -DTGFX_USE_SWIFTSHADER=ON \
      -DTGFX_BUILD_TESTS=ON \
      -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_VERBOSE_MAKEFILE:BOOL=ON ../
if test $? -eq 0; then
  echo "CMake 配置成功"
else
  echo "CMake 配置失败"
  exit 1
fi

# 构建 TGFXFullTest
cmake --build . --target TGFXFullTest -- -j 12 | tee build.log
if test $? -eq 0; then
  echo "TGFXFullTest 构建成功"
else
  echo "TGFXFullTest 构建失败，日志如下："
  cat build.log
  exit 1
fi

# 运行测试
./TGFXFullTest --gtest_output=json:TGFXFullTest.json
if test $? -eq 0; then
  echo "TGFXFullTest 测试成功"
else
  echo "TGFXFullTest 测试失败"
  exit 1
fi

# 收集测试结果
cp -a $WORKSPACE/build/*.json $WORKSPACE/result/
cp -r $WORKSPACE/test/out $WORKSPACE/result
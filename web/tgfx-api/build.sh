#!/bin/bash

# TGFX Basic API WebAssembly Build Script
# TGFX基础API WebAssembly构建脚本

set -e

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}🚀 开始构建 TGFX Basic API WebAssembly库...${NC}"

# 检查Emscripten环境
if ! command -v emcc &> /dev/null; then
    echo -e "${RED}❌ Emscripten未找到！${NC}"
    echo -e "${YELLOW}请先安装并激活Emscripten SDK:${NC}"
    echo "  git clone https://github.com/emscripten-core/emsdk.git"
    echo "  cd emsdk"
    echo "  ./emsdk install latest"
    echo "  ./emsdk activate latest"
    echo "  source ./emsdk_env.sh"
    exit 1
fi

echo -e "${GREEN}✅ Emscripten版本: $(emcc --version | head -n1)${NC}"

# 创建构建目录
BUILD_DIR="build"
WASM_DIR="wasm"

if [ -d "$BUILD_DIR" ]; then
    echo -e "${YELLOW}🧹 清理旧的构建文件...${NC}"
    rm -rf "$BUILD_DIR"
fi

if [ -d "$WASM_DIR" ]; then
    rm -rf "$WASM_DIR"
fi

mkdir -p "$BUILD_DIR"
mkdir -p "$WASM_DIR"

cd "$BUILD_DIR"

# 设置构建类型
BUILD_TYPE="Release"
if [ "$1" = "--debug" ]; then
    BUILD_TYPE="Debug"
    echo -e "${YELLOW}🐛 调试模式构建${NC}"
else
    echo -e "${GREEN}🚀 发布模式构建${NC}"
fi

echo -e "${BLUE}📦 配置CMake...${NC}"

# 配置CMake
emcmake cmake .. \
    -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
    -DTGFX_BUILD_DRAWERS=ON

echo -e "${BLUE}🔨 开始编译...${NC}"

# 编译
emmake make -j

# 检查生成的文件（现在直接生成到wasm目录）
if [ -f "../$WASM_DIR/tgfx-simple-api.js" ] && [ -f "../$WASM_DIR/tgfx-simple-api.wasm" ]; then
    echo -e "${GREEN}✅ 编译成功！${NC}"
    
    # 显示文件大小
    JS_SIZE=$(du -h "../$WASM_DIR/tgfx-simple-api.js" | cut -f1)
    WASM_SIZE=$(du -h "../$WASM_DIR/tgfx-simple-api.wasm" | cut -f1)
    
    echo -e "${GREEN}📊 生成的文件:${NC}"
    echo -e "  JavaScript胶水代码: ${YELLOW}$JS_SIZE${NC}"
    echo -e "  WebAssembly二进制: ${YELLOW}$WASM_SIZE${NC}"
    
    echo -e "${GREEN}📁 文件已直接生成到: ${YELLOW}$WASM_DIR/${NC}"
    
else
    echo -e "${RED}❌ 编译失败！未找到预期的输出文件${NC}"
    echo "预期文件位置: $WASM_DIR/"
    ls -la "../$WASM_DIR/" 2>/dev/null || echo "wasm目录不存在"
    exit 1
fi

cd ..

echo -e "${BLUE}🎉 TGFX Simple API构建完成！${NC}"
echo -e "${GREEN}现在可以在web应用中使用这些文件:${NC}"
echo -e "  ${YELLOW}$WASM_DIR/tgfx-simple-api.js${NC}"
echo -e "  ${YELLOW}$WASM_DIR/tgfx-simple-api.wasm${NC}"
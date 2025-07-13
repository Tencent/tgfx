#!/usr/bin/env bash

set -euo pipefail

# 一键构建 wasm 目标的 libtgfx.a
# 用法:
#   ./build-tgfx-wasm.sh [--debug] [--mt] [--clean]
# 说明:
#   --debug  使用 Debug 构建(默认 Release)
#   --mt     启用 PThreads (wasm-mt)，需要 COOP/COEP 托管环境
#   --clean  清理构建目录后再构建

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
WEB_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
REPO_ROOT="$(cd "${WEB_DIR}/.." && pwd)"
WEB_EDITOR_DIR="${WEB_DIR}/web-code-editor"
DEMO_DIR="${WEB_DIR}/demo"

BUILD_TYPE="Release"
EM_PTHREADS="OFF"
CLEAN_FIRST="0"

for arg in "$@"; do
  case "$arg" in
    --debug)
      BUILD_TYPE="Debug" ;;
    --mt)
      EM_PTHREADS="ON" ;;
    --clean)
      CLEAN_FIRST="1" ;;
    *)
      echo -e "${YELLOW}未知参数: ${arg}${NC}" ;;
  esac
done

if ! command -v emcc >/dev/null 2>&1; then
  echo -e "${RED}未找到 Emscripten(emcc)。请先安装并激活 emsdk:${NC}"
  echo "  git clone https://github.com/emscripten-core/emsdk.git"
  echo "  cd emsdk && ./emsdk install latest && ./emsdk activate latest"
  echo "  source ./emsdk_env.sh"
  exit 1
fi

echo -e "${GREEN}Emscripten: $(emcc --version | head -n1)${NC}"

# 确保 depsync 可用(用于同步三方依赖)
if ! command -v depsync >/dev/null 2>&1; then
  echo -e "${YELLOW}🔧 安装 depsync...${NC}"
  npm install -g depsync >/dev/null 2>&1 || true
fi
if command -v depsync >/dev/null 2>&1; then
  echo -e "${GREEN}depsync: $(depsync --version | head -n1)${NC}"
else
  echo -e "${YELLOW}未找到 depsync，将在 CMake 配置阶段尝试自动同步依赖${NC}"
fi

ARCH_SUFFIX="wasm"
if [[ "${EM_PTHREADS}" == "ON" ]]; then
  ARCH_SUFFIX="wasm-mt"
fi

BUILD_DIR="${WEB_EDITOR_DIR}/cmake-build-${ARCH_SUFFIX}"

if [[ "${CLEAN_FIRST}" == "1" ]]; then
  echo -e "${YELLOW}清理构建目录: ${BUILD_DIR}${NC}"
  rm -rf "${BUILD_DIR}"
fi

mkdir -p "${BUILD_DIR}"

echo -e "${BLUE}配置 CMake (${BUILD_TYPE}, ${ARCH_SUFFIX})...${NC}"
pushd "${BUILD_DIR}" >/dev/null

emcmake cmake "${DEMO_DIR}" \
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
  -DEMSCRIPTEN_PTHREADS="${EM_PTHREADS}" \
  -DTGFX_USE_SWIFTSHADER=OFF \
  -DTGFX_USE_QT=OFF \
  -DTGFX_USE_ANGLE=OFF \
  -DTGFX_USE_OPENGL=ON | cat

echo -e "${BLUE}开始编译...${NC}"
CPU_CORES=4
if command -v sysctl >/dev/null 2>&1; then
  CPU_CORES=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
elif command -v nproc >/dev/null 2>&1; then
  CPU_CORES=$(nproc 2>/dev/null || echo 4)
fi

# 只编译 tgfx 静态库目标，避免编译 demo 可执行文件
cmake --build "${BUILD_DIR}" --target tgfx -j "${CPU_CORES}" | cat

popd >/dev/null

LIB_PATH="${BUILD_DIR}/tgfx/tgfx.a"
if [[ -f "${LIB_PATH}" ]]; then
  SIZE=$(du -h "${LIB_PATH}" | awk '{print $1}')
  echo -e "${GREEN}构建完成: ${NC}${LIB_PATH} (${SIZE})"

  # 自动拷贝到 wasm-assets 目录
  echo -e "${BLUE}拷贝资源到 wasm-assets 目录...${NC}"

  WASM_ASSETS_DIR="${WEB_EDITOR_DIR}/wasm-assets"
  LIBS_DIR="${WASM_ASSETS_DIR}/libs"
  HEADERS_DIR="${WASM_ASSETS_DIR}/headers"

  # 创建目录
  mkdir -p "${LIBS_DIR}"
  mkdir -p "${HEADERS_DIR}"

  # 拷贝静态库
  echo -e "${YELLOW}拷贝静态库: tgfx.a${NC}"
  cp "${LIB_PATH}" "${LIBS_DIR}/"

  # 拷贝头文件
  echo -e "${YELLOW}拷贝头文件目录...${NC}"
  if [[ -d "${REPO_ROOT}/include/tgfx" ]]; then
    # 先清理旧的头文件
    rm -rf "${HEADERS_DIR}/tgfx"
    # 拷贝新的头文件
    cp -r "${REPO_ROOT}/include/tgfx" "${HEADERS_DIR}/"
    HEADER_COUNT=$(find "${HEADERS_DIR}/tgfx" -name "*.h" | wc -l)
    echo -e "${GREEN}已拷贝 ${HEADER_COUNT} 个头文件${NC}"
  else
    echo -e "${RED}头文件目录不存在: ${REPO_ROOT}/include/tgfx${NC}"
  fi

  # 生成头文件清单
  MANIFEST_SCRIPT="${WEB_EDITOR_DIR}/generate-header-manifest.js"
  if [[ -f "${MANIFEST_SCRIPT}" ]]; then
    echo -e "${YELLOW}生成头文件清单...${NC}"
    pushd "${WEB_EDITOR_DIR}" >/dev/null
    if command -v node >/dev/null 2>&1; then
      node generate-header-manifest.js
      echo -e "${GREEN}头文件清单已生成${NC}"
    else
      echo -e "${YELLOW}未找到 Node.js，跳过头文件清单生成${NC}"
    fi
    popd >/dev/null
  fi

  # 显示拷贝结果
  echo -e "${GREEN}资源拷贝完成:${NC}"
  echo -e "  静态库: ${LIBS_DIR}/tgfx.a (${SIZE})"
  if [[ -d "${HEADERS_DIR}/tgfx" ]]; then
    HEADER_COUNT=$(find "${HEADERS_DIR}/tgfx" -name "*.h" | wc -l)
    echo -e "  头文件: ${HEADER_COUNT} 个文件"
  fi
  echo -e "  清单文件: ${WASM_ASSETS_DIR}/header-manifest.js"
  echo ""

  exit 0
else
  echo -e "${RED}构建失败，未找到 libtgfx.a: ${LIB_PATH}${NC}"
  exit 2
fi



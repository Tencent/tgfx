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
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
WEB_EDITOR_DIR="${REPO_ROOT}/web-code-editor"

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

if ! command -v node >/dev/null 2>&1; then
  echo -e "${RED}未找到 Node.js。请先安装 Node.js${NC}"
  exit 1
fi

echo -e "${GREEN}Node.js: $(node --version)${NC}"

ARCH_SUFFIX="wasm"
if [[ "${EM_PTHREADS}" == "ON" ]]; then
  ARCH_SUFFIX="wasm-mt"
fi

# 根目录构建系统的输出路径
BUILD_TYPE_LOWER=$(echo "${BUILD_TYPE}" | tr '[:upper:]' '[:lower:]')
OUT_DIR="${REPO_ROOT}/out/${BUILD_TYPE_LOWER}/web/${ARCH_SUFFIX}"

if [[ "${CLEAN_FIRST}" == "1" ]]; then
  echo -e "${YELLOW}清理构建目录: ${OUT_DIR}${NC}"
  rm -rf "${OUT_DIR}"
fi

echo -e "${BLUE}使用根目录构建系统构建 TGFX (${BUILD_TYPE}, ${ARCH_SUFFIX})...${NC}"
pushd "${REPO_ROOT}" >/dev/null

# 设置构建参数
BUILD_ARGS="-p web -a ${ARCH_SUFFIX}"
if [[ "${BUILD_TYPE}" == "Debug" ]]; then
  BUILD_ARGS="${BUILD_ARGS} -d"
fi

# 使用根目录的构建脚本
node build_tgfx tgfx ${BUILD_ARGS}

popd >/dev/null

# 构建产物路径
LIB_PATH="${OUT_DIR}/tgfx.a"

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
  cp "${LIB_PATH}" "${LIBS_DIR}/tgfx.a"

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
    node generate-header-manifest.js
    echo -e "${GREEN}头文件清单已生成${NC}"
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
  echo -e "${RED}构建失败，未找到 tgfx.a: ${LIB_PATH}${NC}"
  echo -e "${YELLOW}请检查构建过程是否有错误${NC}"
  exit 2
fi
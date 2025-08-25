#!/bin/bash
# start-server.sh - 启动TGFX在线编辑器服务器

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${GREEN}🚀 启动 TGFX 在线编辑器...${NC}"

# 获取脚本所在目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "$SCRIPT_DIR"

echo -e "${BLUE}📁 工作目录: ${SCRIPT_DIR}${NC}"

# 检查必要文件是否存在
if [ ! -f "online-editor.html" ]; then
    echo -e "${RED}online-editor.html 文件不存在！${NC}"
    exit 1
fi

# 检查新架构的资源文件
if [ ! -d "wasm-assets" ]; then
    echo -e "${RED}wasm-assets 目录不存在！请先运行构建脚本${NC}"
    echo -e "${YELLOW}请运行: ../build-tgfx-wasm.sh${NC}"
    exit 1
fi

if [ ! -f "wasm-assets/libs/tgfx.a" ]; then
    echo -e "${RED}TGFX静态库不存在！请先运行构建脚本${NC}"
    echo -e "${YELLOW}💡 请运行: ../build-tgfx-wasm.sh${NC}"
    exit 1
fi

if [ ! -d "wasm-assets/headers/tgfx" ]; then
    echo -e "${RED}TGFX头文件不存在！请先运行构建脚本${NC}"
    echo -e "${YELLOW}💡 请运行: ../build-tgfx-wasm.sh${NC}"
    exit 1
fi

# 检查Emception相关文件
if [ ! -f "emception-adapter.js" ] || [ ! -f "compilation-manager.js" ]; then
    echo -e "${RED}Emception适配器文件不存在！${NC}"
    exit 1
fi

# 显示资源状态
STATIC_LIB_SIZE=$(du -h "wasm-assets/libs/tgfx.a" | awk '{print $1}')
HEADER_COUNT=$(find "wasm-assets/headers/tgfx" -name "*.h" | wc -l)
echo -e "${GREEN}资源状态：${NC}"
echo -e "  静态库: tgfx.a (${STATIC_LIB_SIZE})"
echo -e "  头文件: ${HEADER_COUNT} 个"

# 检查并释放端口
PORT=${PORT:-8081}
echo -e "${YELLOW}检查端口 ${PORT}...${NC}"

# 查找并停止占用端口的进程
PIDS=$(lsof -ti :${PORT} 2>/dev/null || true)
if [ ! -z "$PIDS" ]; then
    echo -e "${YELLOW}⚠端口 ${PORT} 正在被使用，正在释放...${NC}"
    echo $PIDS | xargs kill -9 2>/dev/null || true
    sleep 2
fi

# 启动HTTP服务器（从仓库根目录起服，便于访问 /resources）
echo -e "${BLUE}启动HTTP服务器在端口 ${PORT}...（根目录：${REPO_ROOT}）${NC}"
# Python 3.7+ 支持 -d；若不支持，可改为 pushd "${REPO_ROOT}" 再起服
python3 -m http.server ${PORT} -d "${REPO_ROOT}" &
SERVER_PID=$!

# 等待服务器启动
echo -e "${YELLOW}等待服务器启动...${NC}"
sleep 3

# 检查服务器是否启动成功
if ! curl -s -I http://localhost:${PORT}/web-code-editor/online-editor.html > /dev/null; then
    echo -e "${RED}服务器启动失败！${NC}"
    kill $SERVER_PID 2>/dev/null || true
    exit 1
fi

echo -e "${GREEN}服务器启动成功！${NC}"

# 自动在浏览器中打开页面
URL="http://localhost:${PORT}/web-code-editor/online-editor.html"

# 根据操作系统选择打开命令
if [[ "$OSTYPE" == "darwin"* ]]; then
    # macOS
    open "$URL"
elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
    # Linux
    if command -v xdg-open > /dev/null; then
        xdg-open "$URL"
    elif command -v gnome-open > /dev/null; then
        gnome-open "$URL"
    fi
elif [[ "$OSTYPE" == "msys" ]] || [[ "$OSTYPE" == "cygwin" ]]; then
    # Windows
    start "$URL"
fi

echo -e "${GREEN}TGFX在线编辑器已启动！${NC}"
echo -e "${BLUE}💡 提示：按 Ctrl+C 停止服务器${NC}"

# 显示服务器PID以便管理
echo -e "${YELLOW}🔧 服务器进程ID: ${SERVER_PID}${NC}"

# 等待用户中断
trap "echo -e \"\n${YELLOW}正在停止服务器...${NC}\"; kill $SERVER_PID 2>/dev/null || true; echo -e \"${GREEN}服务器已停止${NC}\"; exit 0" INT

# 保持脚本运行
echo -e "${GREEN}🔄 服务器正在运行... (按 Ctrl+C 停止)${NC}"
wait $SERVER_PID

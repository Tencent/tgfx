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
cd "$SCRIPT_DIR"

echo -e "${BLUE}📁 工作目录: ${SCRIPT_DIR}${NC}"

# 检查必要文件是否存在
if [ ! -f "online-editor.html" ]; then
    echo -e "${RED}❌ online-editor.html 文件不存在！${NC}"
    exit 1
fi

if [ ! -d "wasm" ]; then
    echo -e "${RED}❌ wasm 目录不存在！请先编译WASM模块${NC}"
    exit 1
fi

if [ ! -f "wasm/tgfx-simple-api.js" ] || [ ! -f "wasm/tgfx-simple-api.wasm" ]; then
    echo -e "${RED}❌ WASM文件不存在！请先运行 ./build.sh 编译${NC}"
    exit 1
fi

# 检查并释放端口
PORT=8081
echo -e "${YELLOW}🔍 检查端口 ${PORT}...${NC}"

# 查找并停止占用端口的进程
PIDS=$(lsof -ti :${PORT} 2>/dev/null || true)
if [ ! -z "$PIDS" ]; then
    echo -e "${YELLOW}⚠️  端口 ${PORT} 正在被使用，正在释放...${NC}"
    echo $PIDS | xargs kill -9 2>/dev/null || true
    sleep 2
fi

# 启动HTTP服务器
echo -e "${BLUE}🌐 启动HTTP服务器在端口 ${PORT}...${NC}"
python3 -m http.server ${PORT} &
SERVER_PID=$!

# 等待服务器启动
echo -e "${YELLOW}⏳ 等待服务器启动...${NC}"
sleep 3

# 检查服务器是否启动成功
if ! curl -s -I http://localhost:${PORT}/online-editor.html > /dev/null; then
    echo -e "${RED}❌ 服务器启动失败！${NC}"
    kill $SERVER_PID 2>/dev/null || true
    exit 1
fi

echo -e "${GREEN}✅ 服务器启动成功！${NC}"

# 显示可用页面
echo -e "${BLUE}📋 可用页面：${NC}"
echo "  🎨 在线编辑器: http://localhost:${PORT}/online-editor.html"
echo "  🧪 API测试页面: http://localhost:${PORT}/test-simple-api.html"
echo "  📁 文件列表: http://localhost:${PORT}/"

# 自动在浏览器中打开在线编辑器
URL="http://localhost:${PORT}/online-editor.html"
echo -e "${GREEN}🌐 正在浏览器中打开: ${URL}${NC}"

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

echo -e "${GREEN}🎉 TGFX在线编辑器已启动！${NC}"
echo -e "${YELLOW}📝 使用说明：${NC}"
echo "  1. 点击示例按钮加载代码"
echo "  2. 编辑C++代码"
echo "  3. 点击运行按钮查看结果"
echo ""
echo -e "${BLUE}💡 提示：按 Ctrl+C 停止服务器${NC}"

# 显示服务器PID以便管理
echo -e "${YELLOW}🔧 服务器进程ID: ${SERVER_PID}${NC}"

# 等待用户中断
trap "echo -e \"\n${YELLOW}🛑 正在停止服务器...${NC}\"; kill $SERVER_PID 2>/dev/null || true; echo -e \"${GREEN}✅ 服务器已停止${NC}\"; exit 0" INT

# 保持脚本运行
echo -e "${GREEN}🔄 服务器正在运行... (按 Ctrl+C 停止)${NC}"
wait $SERVER_PID
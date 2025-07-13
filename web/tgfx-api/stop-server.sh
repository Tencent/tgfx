#!/bin/bash
# stop-server.sh - 停止TGFX在线编辑器服务器

echo "🛑 正在停止TGFX在线编辑器服务器..."

# 停止所有Python HTTP服务器
pkill -f "python3 -m http.server" && echo "✅ 服务器已停止" || echo "ℹ️  没有找到运行中的服务器"

# 释放8081端口
lsof -ti :8081 | xargs kill -9 2>/dev/null && echo "🔓 端口8081已释放" || true

echo "✅ 清理完成"
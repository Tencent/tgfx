#!/bin/bash
# PostToolUse hook: 记录 Write/Edit/MultiEdit 修改的文件

INPUT=$(cat)
TOOL_NAME=$(echo "$INPUT" | jq -r '.tool_name')
FILE_PATH=$(echo "$INPUT" | jq -r '.tool_input.file_path // empty')

# 只处理文件修改工具
if [[ "$TOOL_NAME" != "Write" && "$TOOL_NAME" != "Edit" && "$TOOL_NAME" != "MultiEdit" ]]; then
    exit 0
fi

# 记录文件路径到临时文件
if [[ -n "$FILE_PATH" ]]; then
    echo "$FILE_PATH" >> /tmp/codebuddy-modified-files.txt
fi

exit 0

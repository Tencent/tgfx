#!/bin/bash
# PostToolUse hook: 检查 Write/Edit 后是否有未提交的变更

# 读取 stdin 获取工具信息
INPUT=$(cat)
TOOL_NAME=$(echo "$INPUT" | jq -r '.tool_name')
FILE_PATH=$(echo "$INPUT" | jq -r '.tool_input.file_path // .tool_input.filePath // empty')

# 只检查 Write/Edit/MultiEdit 工具
if [[ "$TOOL_NAME" != "Write" && "$TOOL_NAME" != "Edit" && "$TOOL_NAME" != "MultiEdit" ]]; then
    exit 0
fi

# 检查文件是否有未提交的变更
if [[ -n "$FILE_PATH" ]] && git diff --name-only HEAD -- "$FILE_PATH" 2>/dev/null | grep -q .; then
    echo '{"decision":"block","reason":"文件已修改但未提交。请立即执行 git commit --only '"$FILE_PATH"' -m \"{Commit 信息}\" 提交变更。"}' 
    exit 2
fi

exit 0

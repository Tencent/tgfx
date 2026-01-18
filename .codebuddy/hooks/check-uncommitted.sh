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

# 检查文件是否有未提交的变更（包括新文件）
if [[ -n "$FILE_PATH" ]]; then
    # 检查是否是未跟踪的新文件，或已跟踪文件有变更
    if git ls-files --others --exclude-standard -- "$FILE_PATH" 2>/dev/null | grep -q . || \
       git diff --name-only HEAD -- "$FILE_PATH" 2>/dev/null | grep -q .; then
        cat <<EOF
{"decision":"block","reason":"!! AUTO-COMMIT REQUIRED !! 文件 $FILE_PATH 已修改但未提交。根据 Git.md 规则，你必须立即执行 commit，不得进行任何其他操作。"}
EOF
        exit 2
    fi
fi

exit 0

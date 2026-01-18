#!/bin/bash
# Stop hook: 对话结束时检查 AI 修改的文件是否已提交

TRACK_FILE="/tmp/codebuddy-modified-files.txt"

# 如果没有记录文件，说明本次会话没有修改任何文件
if [[ ! -f "$TRACK_FILE" ]]; then
    exit 0
fi

# 读取本次会话修改的文件列表（去重）
MODIFIED_FILES=$(sort -u "$TRACK_FILE")

# 清理临时文件
rm -f "$TRACK_FILE"

# 检查这些文件是否有未提交的变更
UNCOMMITTED=""
for FILE in $MODIFIED_FILES; do
    # 检查是否是未跟踪的新文件，或已跟踪文件有变更
    if git ls-files --others --exclude-standard -- "$FILE" 2>/dev/null | grep -q . || \
       git diff --name-only HEAD -- "$FILE" 2>/dev/null | grep -q .; then
        UNCOMMITTED="$UNCOMMITTED $FILE"
    fi
done

if [[ -n "$UNCOMMITTED" ]]; then
    cat <<EOF
{"decision":"block","reason":"!! AUTO-COMMIT REQUIRED !! 以下文件已修改但未提交：${UNCOMMITTED}。请执行 git commit --only {文件列表} -m \"{Commit 信息}\" 提交变更。"}
EOF
    exit 2
fi

exit 0

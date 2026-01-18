#!/bin/bash
# Stop hook: 对话结束时检查 AI 修改的文件是否已提交

MODIFIED_DIR="$CODEBUDDY_PROJECT_DIR/.codebuddy/.modified"

# 如果没有记录目录，说明本次会话没有修改任何文件
if [[ ! -d "$MODIFIED_DIR" ]]; then
    exit 0
fi

# 读取本次会话修改的文件列表
UNCOMMITTED=""
for MARKER in "$MODIFIED_DIR"/*; do
    [[ -f "$MARKER" ]] || continue
    FILE_PATH=$(cat "$MARKER")
    
    # 检查文件是否有未提交的变更
    if git ls-files --others --exclude-standard -- "$FILE_PATH" 2>/dev/null | grep -q . || \
       git diff --name-only HEAD -- "$FILE_PATH" 2>/dev/null | grep -q .; then
        UNCOMMITTED="$UNCOMMITTED $FILE_PATH"
    fi
done

# 清理标记目录
rm -rf "$MODIFIED_DIR"

if [[ -n "$UNCOMMITTED" ]]; then
    cat <<EOF
{"decision":"block","reason":"!! AUTO-COMMIT REQUIRED !! 以下文件已修改但未提交：${UNCOMMITTED}。请执行 git commit --only {文件列表} -m \"{Commit 信息}\" 提交变更。"}
EOF
    exit 2
fi

exit 0

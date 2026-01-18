#!/bin/bash
# Stop hook: 对话结束时检查是否有未提交的变更

# 获取所有未提交的变更文件（已跟踪的修改 + 未跟踪的新文件）
MODIFIED=$(git diff --name-only HEAD 2>/dev/null)
UNTRACKED=$(git ls-files --others --exclude-standard 2>/dev/null)

# 合并去重
ALL_FILES=$(echo -e "${MODIFIED}\n${UNTRACKED}" | grep -v '^$' | sort -u)

if [[ -n "$ALL_FILES" ]]; then
    # 格式化文件列表
    FILE_LIST=$(echo "$ALL_FILES" | tr '\n' ' ')
    cat <<EOF
{"decision":"block","reason":"!! AUTO-COMMIT REQUIRED !! 以下文件已修改但未提交：${FILE_LIST}。请执行 git commit --only {文件列表} -m \"{Commit 信息}\" 提交变更。"}
EOF
    exit 2
fi

exit 0

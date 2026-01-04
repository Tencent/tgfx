---
description: 当用户发送Github PR链接、要求审查代码、或需要提交代码、发起PR时
alwaysApply: false
enabled: true
updatedAt: 2025-12-10T02:54:32.098Z
provider: 
---

## 发起 Github PR 流程
- 运行 `./codeformat.sh` 格式化代码，忽略输出的报错，只要运行就会完成格式化
- 按照代码审查要点评审本地变更代码，确认无问题后准备 PR 信息
- 输出以下内容供确认：
    - **分支名称**：格式同分支命名规则
    - **PR 标题**：英语，格式同 commit 信息要求
    - **PR 描述**：中文简要说明变更内容
- 确认后创建分支、推送代码、并严格按照确认过的标题和描述执行 `gh pr create` 发起 PR，禁止修改任何内容或添加额外格式
- 对已发起的 PR 补充提交时，使用正常追加提交，禁止 force push 覆盖

## Github PR Review 流程
- 用 `gh` 命令获取 PR 变更和评论，结合本地代码理解上下文
- 输出变更总结
- 输出优化的 PR 标题，使其更符合作为 commit 信息的要求
- 按照代码审查要点对变更代码进行审查
- 验证已有评论是否真正修复（重点关注我提过的评论）
- 新问题或未解决问题带序号汇总，经用户确认后添加**中文行级评论**

## Github PR 行级评论
- 仅允许行级评论，禁止 `gh pr comment`
- 使用 `gh api` 配合 `--input -` 和 heredoc 传递 JSON
```bash
gh api repos/{owner}/{repo}/pulls/{pr}/reviews --input - <<'EOF'
{"commit_id":"HEAD_SHA","event":"COMMENT","comments":[{"path":"文件路径","line":行号,"side":"RIGHT","body":"内容"}]}
EOF
```

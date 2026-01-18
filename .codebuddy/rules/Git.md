---
description: Git 操作规范
alwaysApply: true
---

## **!! IMPORTANT - 操作限制**

- **NEVER** 自动执行 `git stash`、`git reset`、`git checkout` 等改变暂存区或工作区状态的命令，除非**暂存区为空**或**用户明确要求**
- **NEVER** 在 main 分支直接提交推送代码，必须通过 PR 流程

## **!! CRITICAL - 自动提交（必须执行）**

**每次使用 Write 或 Edit 工具修改文件后，必须立即执行 commit，不得有任何例外。**

执行流程：

1. 若当前在 main 分支，先创建新分支：`git checkout -b {分支名称}`
2. **立即**使用 `git commit --only {修改的文件} -m "{Commit 信息}"` 提交

违规场景（禁止）：
- ❌ 修改文件后继续其他操作而不 commit
- ❌ 修改多个文件后批量 commit
- ❌ 等用户要求才 commit

正确场景：
- ✅ Write/Edit 工具返回成功后，下一个动作必须是 git commit

> 注：`git commit --only` 只提交指定文件，不影响现有暂存区和工作区状态。

## 分支命名

格式：`feature/{username}_模块名` 或 `bugfix/{username}_模块名`

- `{username}`：GitHub 用户 ID，全小写
- 模块名用下划线连接，最多两个单词

## Commit 信息

120 字符内的英语概括，以英文句号结尾，中间无其他标点，侧重描述用户可感知的变化


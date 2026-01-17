---
description: Git 操作规范
alwaysApply: true
---

## **!! IMPORTANT - 操作限制**

- **NEVER** 自动执行 `git stash`、`git reset`、`git checkout` 等改变暂存区或工作区状态的命令，除非**暂存区为空**或**用户明确要求**
- **NEVER** 在 main 分支直接提交推送代码，必须通过 PR 流程

## **!! IMPORTANT - 自动提交**

每次编辑文件后，立即提交变更（仅 commit，不自动推送）：

1. 若当前在 main 分支，先创建新分支：`git checkout -b {分支名称}`
2. 使用 `git commit --only {修改的文件} -m "{Commit 信息}"` 提交

> 注：`git commit --only` 只提交指定文件，不影响现有暂存区和工作区状态。

## 分支命名

格式：`feature/{username}_模块名` 或 `bugfix/{username}_模块名`

- `{username}`：GitHub 用户 ID，全小写
- 模块名用下划线连接，最多两个单词

## Commit 信息

120 字符内的英语概括，以英文句号结尾，中间无其他标点，侧重描述用户可感知的变化


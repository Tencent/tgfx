---
description: Git 操作规范
alwaysApply: true
---

## **!! CRITICAL - 自动提交**

完成用户请求后，若有文件被修改，执行提交（仅 commit，**不自动推送**）：

```bash
git commit --only {文件1} {文件2} ... -m "{Commit 信息}"
```

若当前在 main 分支，先创建新分支再提交。

> 注：`git commit --only` 提交指定文件的工作区完整内容。若要修改的文件已有用户未提交的变更（`git status` 显示该文件有修改），需先询问用户是否继续，因为自动提交会将用户的修改一并提交。

## **!! IMPORTANT - 操作限制**

- **NEVER** 自动执行 `git stash`、`git reset`、`git checkout` 等改变暂存区或工作区状态的命令，除非**暂存区为空**或**用户明确要求**
- **NEVER** 在 main 分支直接提交推送代码，必须通过 PR 流程

## 分支命名

格式：`feature/{username}_模块名` 或 `bugfix/{username}_模块名`

- `{username}`：GitHub 用户 ID，全小写
- 模块名用下划线连接，最多两个单词

## Commit 信息

120 字符内的英语概括，以英文句号结尾，中间无其他标点，侧重描述用户可感知的变化

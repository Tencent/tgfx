---
description: 变基到主干 - 将未推送的 commit 和本地变更迁移到基于最新主干的新分支
---

# 变基到主干

将当前分支上未推送的 commit、暂存区和工作区变更迁移到基于最新 main 拉取的新分支上。

---

## 前置检查

```bash
ORIGINAL_BRANCH=$(git branch --show-current)
git fetch origin main

# 未推送的 commit（优先用远程跟踪分支，若无则用 origin/main），按时间正序
git log @{upstream}..HEAD --oneline --reverse 2>/dev/null || git log origin/main..HEAD --oneline --reverse

# 暂存区和工作区变更（含未跟踪文件）
git status --porcelain
```

记录未推送的 commit 列表（按时间正序）和本地变更状态，供后续步骤使用。

若无未推送的 commit **且** 无任何本地变更（暂存区、工作区、未跟踪文件均为空），提示无需迁移，终止流程。

---

## 第一步：保存暂存区和工作区变更

若前置检查中有本地变更，执行：

```bash
git stash push --include-untracked -m "onto-main-temp"
```

---

## 第二步：创建新分支

```bash
git checkout main
git pull --rebase origin main
git checkout -b {新分支名称}
```

**新分支命名**：根据待迁移的所有变更内容（commit + 暂存区 + 工作区）分析，按分支命名规范生成。

---

## 第三步：迁移 commit

若前置检查中的 commit 列表不为空，逐条 cherry-pick：

```bash
git cherry-pick {commit-sha}
```

若遇到冲突，按**冲突处理**流程处理：

- 用户确认修复后，执行 `git add .` 并 `git cherry-pick --continue`
- 若用户选择跳过该 commit，执行 `git cherry-pick --skip`
- 若用户选择中止，执行 `git cherry-pick --abort`，切回原分支并还原 stash，**终止流程**

---

## 第四步：还原暂存区和工作区

若第一步有 stash，执行：

```bash
git stash pop --index
```

> 注：`stash pop` 成功后会自动删除该 stash 条目。

若遇到冲突，按**冲突处理**流程处理，解决后执行 `git stash drop`。

**注意**：解决后不修改暂存区状态，保持还原后的原始状态。

---

## 第五步：清理原分支

若原分支是 main，跳过此步骤。

检查原分支是否有已合并的 PR：

```bash
gh pr list --head "$ORIGINAL_BRANCH" --state merged --json number
```

若存在已合并的 PR，删除本地原分支：

```bash
git branch -D $ORIGINAL_BRANCH
```

---

## 输出

```
**原分支**：{原分支名}
**新分支**：{新分支名}
**迁移 commit 数**：{数量}
**原分支状态**：{已删除 / 保留}（原分支为 main 时不显示此项）
```

---

## 附：冲突处理流程

1. 列出冲突文件
2. 阅读每个冲突文件的代码，分析冲突原因
3. 提供自动修复方案（说明如何解决每个冲突）
4. 询问用户是否执行自动修复
5. 用户确认后执行修复

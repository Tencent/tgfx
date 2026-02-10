---
description: 提交 PR - 自动识别新建或追加提交
---

# 提交 PR

严格按以下步骤顺序执行。

---

## 前置检查

以下三组命令**并行执行**（使用多个 Bash tool call 同时发起）：

```bash
# 并行 1：fetch + 分支信息
git fetch origin main && git branch --show-current
```

```bash
# 并行 2：查询当前分支是否有开启的 PR（用上一步的分支名替换）
gh pr list --head "$(git branch --show-current)" --state open --json number,url
```

```bash
# 并行 3：获取 GitHub username
gh api user --jq '.login'
```

记录：当前分支、是否有开启的 PR、GitHub username（供新建模式使用）。

根据结果选择模式：

| 当前分支 | 已有开启的 PR | 模式 |
|----------|---------------|------|
| main | - | **新建模式** |
| 非 main | 有 | **追加模式** |
| 非 main | 无 | **新建模式** |

---

## 第一步：确定提交范围

运行 `git status --porcelain`：
- 若有输出：
  - 若暂存区有内容，且工作区也有内容（含未跟踪文件）：询问用户选择局部提交或全部提交
  - 否则：全部提交
- 若无输出：记录为**无本地变更**，跳过第二、三步

**判断方法**：第一列非空格为暂存区有内容；第二列非空格或以 `??` 开头为工作区有内容。

若为**局部提交**，使用 `git diff --cached --name-only` 记录**暂存区文件列表**供后续步骤使用。

---

## 第二步：格式化代码

收集待提交的源文件列表（`.cpp`、`.c`、`.h`、`.mm`、`.m`），仅对这些文件执行 `clang-format -i`，**不要**运行 `./codeformat.sh`（全量格式化太慢）。

待提交文件来源：
- **全部提交**：`git diff --name-only HEAD` + `git ls-files --others --exclude-standard`（工作区所有变更 + 未跟踪文件）
- **局部提交**：第一步记录的暂存区文件列表

从中筛选源文件后执行：

```bash
# FILES 为筛选后的源文件列表（空格分隔）
clang-format -i {FILES}
```

若没有需要格式化的源文件则跳过。

格式化后根据第一步结果处理暂存区：

| 第一步结果 | 处理方式 |
|------------|----------|
| 全部提交 | 执行 `git add .` 将所有变更加入暂存区 |
| 局部提交 | 仅将第一步记录的文件重新加入暂存区（`git add {文件}`） |

---

## 第三步：生成 Commit 信息

```bash
git diff --cached
```

根据暂存区变更生成 **Commit 信息**：英语，120 字符内，以句号结尾，侧重描述用户可感知的变化。

**!! IMPORTANT**：将 `git diff --cached` 的结果缓存，第四步新建模式"分析完整变更"时直接复用，不再重复执行。

---

## 第四步：提交并推送

### 追加模式

检查未推送的 commit：

```bash
git log origin/{当前分支}..HEAD --oneline
```

| 暂存区 | 未推送 commit | 操作 |
|--------|---------------|------|
| 有内容 | - | 提交并推送 |
| 无内容 | 有 | 仅推送 |
| 无内容 | 无 | 提示"无新变更需要提交"，流程结束 |

```bash
# 若暂存区有内容，提交并推送
git commit -m "{Commit 信息}" && git push

# 若暂存区无内容但有未推送 commit，仅推送
git push
```

输出：

```
**Commit**：{Commit 信息}（若有新提交）

**PR 链接**（已追加提交）：{PR 链接}
```

### 新建模式

#### 1. 分析完整变更

分析 PR 包含的所有变更：

```bash
git log origin/main..HEAD --oneline   # 已有 commit
# 本次暂存区变更：直接复用第三步缓存的 git diff --cached 结果
```

若已有 commit 为空且暂存区无内容，提示无变更，终止流程。

#### 2. 生成 PR 信息

根据上述完整变更（已有 commit + 暂存区变更）生成：

- **分支名称**：`feature/{username}_模块名` 或 `bugfix/{username}_模块名`（`{username}` 为前置检查获取的 GitHub 用户 ID 全小写，模块名用下划线连接，最多两个单词）
- **PR 标题**：英语，120 字符内，以句号结尾，侧重描述用户可感知的变化
- **PR 描述**：中文，简要说明变更内容和目的

#### 3. 处理分支并提交推送

切换到新分支：

| 当前分支 | 操作 |
|----------|------|
| main | `git checkout -b {分支名称}` |
| 非 main | `git branch -m {分支名称}` |

提交并推送：

```bash
# 若暂存区有内容，先提交
git commit -m "{Commit 信息}"

# 推送并创建 PR
git push -u origin {分支名称}
gh pr create --title "{PR 标题}" --body "{PR 描述}"
```

输出：

```
**PR 标题**：{PR 标题}

**PR 描述**：{PR 描述}

**PR 链接**（新建）：{PR 链接}
```

---

## **!! IMPORTANT - 重要限制**

- **NEVER** 执行 force push

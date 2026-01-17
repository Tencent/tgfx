---
description: 提交 PR - 自动识别新建或追加提交
---

# 提交 PR

严格按以下步骤顺序执行。

---

## 前置检查

```bash
git fetch origin main
CURRENT_BRANCH=$(git branch --show-current)
gh pr list --head "$CURRENT_BRANCH" --state open --json number,url
```

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
- 若无输出：记录为**无本地变更**

**判断方法**：第一列非空格为暂存区有内容；第二列非空格或以 `??` 开头为工作区有内容。

若为**局部提交**，记录**暂存区文件列表**供后续步骤使用。

---

## 第二步：格式化代码

```bash
./codeformat.sh
```

忽略输出的报错信息，只要运行就会完成格式化。

格式化后根据第一步结果处理暂存区：

| 第一步结果 | 处理方式 |
|------------|----------|
| 全部提交 | 执行 `git add .` 将所有变更加入暂存区 |
| 局部提交 | 仅将第一步记录的文件重新加入暂存区（`git add {文件}`） |
| 无本地变更 | 若格式化产生了变更，执行 `git add .` 加入暂存区 |

---

## 第三步：生成 Commit 信息

```bash
git diff --cached
```

若暂存区无内容，跳过此步骤。

根据暂存区变更生成 **Commit 信息**：英语，120 字符内，以句号结尾，侧重描述用户可感知的变化。

---

## 第四步：提交并推送

### 追加模式

若暂存区无内容，提示"无新变更需要提交"，流程结束。

```bash
git commit -m "{Commit 信息}"
git push
```

输出：

```
**Commit**：{Commit 信息}

**PR 链接**（已追加提交）：{PR 链接}
```

### 新建模式

#### 1. 分析完整变更

```bash
git log origin/main..HEAD --oneline   # 已有未推送的 commit
git diff --cached                      # 本次暂存区变更（复用第三步结果）
```

若已有 commit 为空且暂存区无内容，提示无变更，终止流程。

#### 2. 生成 PR 信息

根据完整变更生成：

- **分支名称**：`feature/{username}_模块名` 或 `bugfix/{username}_模块名`（`{username}` 为 GitHub 用户 ID 全小写，模块名用下划线连接，最多两个单词）
- **PR 标题**：英语，120 字符内，以句号结尾，侧重描述用户可感知的变化
- **PR 描述**：中文，简要说明变更内容和目的

#### 3. 处理分支

| 当前分支 | 操作 |
|----------|------|
| main | `git checkout -b {分支名称}` |
| 非 main | `git branch -m {分支名称}` |

#### 4. 提交并推送

```bash
# 若暂存区有内容
git commit -m "{Commit 信息}"

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

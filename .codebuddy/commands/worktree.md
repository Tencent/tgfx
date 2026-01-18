---
description: 管理 Git Worktree - 创建、切换或清理 worktree，自动同步测试缓存
---

# Git Worktree 管理

管理 git worktree，支持创建、切换和清理操作，创建时自动同步测试缓存目录。

---

## 参数解析

| $ARGUMENTS | 操作 |
|------------|------|
| 空 | 列出现有 worktree，询问用户进入或删除 |
| `{name}` | 进入该 worktree（不存在则创建） |

名称规范化：纯数字如 `1` 自动转为 `wt1`。

---

## 无参数模式

### 1. 列出现有 worktree

```bash
MAIN_REPO=$(git worktree list --porcelain | head -1 | sed 's/worktree //')
REPO_NAME=$(basename "$MAIN_REPO")
git worktree list
```

展示 worktree 列表给用户，询问用户要执行的操作：
- 进入某个已存在的 worktree
- 删除某个 worktree

### 2. 进入 worktree

切换到用户选择的 worktree 目录，并同步测试缓存（见「同步缓存目录」）。

### 3. 删除 worktree

若当前工作目录在待删除的 worktree 内，先切换到主仓库：

```bash
cd "$MAIN_REPO"
```

移除 worktree：

```bash
git worktree remove "{用户选择的 worktree 路径}" --force
```

---

## 有参数模式：进入或创建 worktree

### 1. 获取主仓库信息

```bash
MAIN_REPO=$(git worktree list --porcelain | head -1 | sed 's/worktree //')
REPO_NAME=$(basename "$MAIN_REPO")
WT_PATH="$MAIN_REPO/../$REPO_NAME-{名称}"
```

### 2. 检查 worktree 是否存在

```bash
test -d "$WT_PATH" && echo "exists" || echo "not found"
```

- **若存在**：同步缓存目录后切换到该 worktree（跳到步骤 4）
- **若不存在**：创建新 worktree（继续步骤 3）

### 3. 创建 worktree

```bash
git fetch origin main
git worktree add "$WT_PATH" origin/main
```

### 4. 同步缓存目录

从主仓库拷贝测试缓存（若存在）：

```bash
# 拷贝 baseline 缓存
if [ -d "$MAIN_REPO/test/baseline/.cache" ]; then
    mkdir -p "$WT_PATH/test/baseline"
    cp -r "$MAIN_REPO/test/baseline/.cache" "$WT_PATH/test/baseline/"
    echo "已同步 test/baseline/.cache"
fi

# 拷贝 test/out
if [ -d "$MAIN_REPO/test/out" ]; then
    mkdir -p "$WT_PATH/test"
    cp -r "$MAIN_REPO/test/out" "$WT_PATH/test/"
    echo "已同步 test/out"
fi
```

### 5. 切换到 worktree

```bash
cd "$WT_PATH"
pwd
```

输出（新建时）：

```
**Worktree 已创建**：{WT_PATH}
**基于分支**：origin/main
**已同步缓存**：{同步的目录列表}
```

输出（已存在时）：

```
**已切换到 worktree**：{WT_PATH}
**已同步缓存**：{同步的目录列表}
```

---

## 重要限制

- **NEVER** 删除主仓库
- **NEVER** 在主仓库路径上执行 worktree remove
- 创建 worktree 时始终基于 `origin/main`

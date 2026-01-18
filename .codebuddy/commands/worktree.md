---
description: 管理 Git Worktree - 创建、切换或清理 worktree，自动同步测试缓存
---

# Git Worktree 管理

管理 git worktree，支持创建、切换和清理操作，创建时自动同步测试缓存目录。

---

## 参数解析

根据 `$ARGUMENTS` 解析操作和名称：

| $ARGUMENTS | 操作 |
|------------|------|
| 空 | list |
| `1` 或 `wt1` | 进入 wt1（不存在则创建） |
| `rm 1` 或 `rm wt1` | remove wt1 |

名称规范化：纯数字 `1` 自动转为 `wt1`。

---

## 操作：list

```bash
git worktree list
```

列出所有 worktree 并展示给用户。

---

## 操作：进入（或创建）worktree

### 1. 获取主仓库信息

```bash
MAIN_REPO=$(git worktree list --porcelain | head -1 | sed 's/worktree //')
REPO_NAME=$(basename "$MAIN_REPO")
WT_PATH="$MAIN_REPO/../$REPO_NAME-{名称}"
echo "MAIN_REPO: $MAIN_REPO"
echo "WT_PATH: $WT_PATH"
```

### 2. 检查 worktree 是否存在

```bash
test -d "$WT_PATH" && echo "exists" || echo "not found"
```

- **若存在**：直接切换到该 worktree（跳到步骤 5）
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
已切换到 worktree：{WT_PATH}
```

---

## 操作：remove

### 1. 获取路径

```bash
MAIN_REPO=$(git worktree list --porcelain | head -1 | sed 's/worktree //')
REPO_NAME=$(basename "$MAIN_REPO")
WT_PATH="$MAIN_REPO/../$REPO_NAME-{名称}"
```

### 2. 检查是否存在

```bash
test -d "$WT_PATH" && echo "exists" || echo "not found"
```

若不存在，提示 worktree 不存在，**终止流程**。

### 3. 检查当前位置

若当前工作目录在该 worktree 内，先切换到主仓库：

```bash
cd "$MAIN_REPO"
```

### 4. 移除 worktree

```bash
git worktree remove "$WT_PATH" --force
```

输出：

```
**已移除 Worktree**：{WT_PATH}
**当前目录**：{当前工作目录}
```

---

## 重要限制

- **NEVER** 删除主仓库
- **NEVER** 在主仓库路径上执行 worktree remove
- 创建 worktree 时始终基于 `origin/main`

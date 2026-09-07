# TGFX 多后端截图测试方案

**日期**：2026-05-21  
**状态**：方案评审

---

## 一、背景

TGFX 正在从单一 OpenGL 后端扩展到 OpenGL / Vulkan / Metal / D3D 四后端。现有截图测试系统仅支持单后端，无法满足多后端并行开发和持续集成的需求。

**核心矛盾**：
- 不同后端的渲染输出存在规范允许的精度差异，无法共用同一份 baseline
- 各后端代码分布在不同分支，尚未全部合入 main
- 开发者设备不全（Mac 无法跑 D3D，Win 无法跑 Metal）
- CI 环境无独立 GPU（依赖软件渲染器）

---

## 二、方案概述

**一句话**：在现有 MD5 + cache 机制上做路径隔离，每个后端独立维护 baseline，核心比较逻辑不变。

**改动量**：约 30 行 C++ 代码 + 脚本重写 + CI 配置重写。

---

## 三、平台能力矩阵

各平台可运行的后端及渲染方式（编译时选择，互斥）：

| 平台 | 后端 | 软件渲染 | 真机 GPU | 软件渲染编译参数 | 真机 GPU 编译参数 |
|---|---|---|---|---|---|
| **Mac** | OpenGL | ✅ SwiftShader (EGL) | ✅ Apple GPU (CGL) | `-DTGFX_USE_SWIFTSHADER=ON` | 默认（不传后端参数） |
| **Mac** | Vulkan | ✅ SwiftShader (ICD) | ✅ MoltenVK | `-DTGFX_USE_VULKAN=ON -DTGFX_USE_SWIFTSHADER=ON` | `-DTGFX_USE_VULKAN=ON` |
| **Mac** | Metal | ❌ 无 | ✅ Apple GPU | — | `-DTGFX_USE_METAL=ON` |
| **Win** | OpenGL | ✅ SwiftShader (EGL) | ✅ 真机 GPU (WGL) | `-DTGFX_USE_SWIFTSHADER=ON` | 默认（不传后端参数） |
| **Win** | Vulkan | ✅ SwiftShader (ICD) | ✅ 真机驱动 | `-DTGFX_USE_VULKAN=ON -DTGFX_USE_SWIFTSHADER=ON` | `-DTGFX_USE_VULKAN=ON` |
| **Win** | D3D | ✅ WARP | ✅ 真机 GPU | `-DTGFX_USE_D3D=ON`（代码中选 WARP adapter） | `-DTGFX_USE_D3D=ON`（代码中选真机 adapter） |

**平台限制**：Mac 无法跑 D3D；Win 无法跑 Metal。

**说明**：
- 每次编译只能启用一个后端（互斥）
- OpenGL 默认不传参数时使用真机 GPU（Mac=CGL，Win=WGL），传 `-DTGFX_USE_SWIFTSHADER=ON` 则走 SwiftShader
- CI 上 GL/VK 使用 SwiftShader（确定性），Metal 使用真机 GPU，D3D 使用 WARP

---

## 四、CI 策略

### CI 后端分配

CI 机器无独立 GPU，只能用软件渲染器。SwiftShader 跨平台确定性一致，同一后端无需两个平台各跑一次。

| CI Runner | 后端 | 渲染器 | 说明 |
|---|---|---|---|
| macos-14 | OpenGL | SwiftShader | SwiftShader 跨平台一致，选 Mac 跑即可 |
| macos-14 | Metal | Apple GPU | Metal 只有 Mac 能跑（macos-14 有 M1 GPU） |
| windows-latest | Vulkan | SwiftShader | SwiftShader 跨平台一致，选 Win 跑即可 |
| windows-latest | D3D | WARP | D3D 只有 Win 能跑 |

**注意**：这只是 CI 的分配策略。开发者本地 Mac/Win 都能跑 GL 和 VK（无论 SwiftShader 还是真机 GPU），不受 CI 分配限制。

### CI 规则

- 所有 PR 和 push **强制运行全部 4 个 job**
- 4 个 job 并行执行
- 任一后端 FAIL 则 PR 不可合入

```
mac-opengl   ─┐
mac-metal    ─┼─ 并行 ─→ 全绿才可合入
win-vulkan   ─┤
win-d3d      ─┘
```

---

## 五、目录结构

```
test/baseline/
├── version.json                         ← 所有后端共用（记录 key → commit hash）
└── .cache/
    ├── opengl/md5.json                  ← 真机 GPU GL（开发者本地）
    ├── opengl-swiftshader/md5.json      ← SwiftShader GL（CI）
    ├── vulkan/md5.json                  ← 真机 GPU VK（开发者本地）
    ├── vulkan-swiftshader/md5.json      ← SwiftShader VK（CI）
    ├── metal/md5.json                   ← Apple GPU（Mac）
    └── d3d/md5.json                     ← WARP 或真机（Win）
```

- `version.json`：记录每个测试被 accept 时的 commit hash，与后端无关，只需一份
- `.cache/{backend-name}/md5.json`：记录该后端+渲染器组合下每个测试的像素 MD5，互不干扰

### TGFX_BACKEND_NAME 取值

| 编译参数 | TGFX_BACKEND_NAME | 用于 |
|---|---|---|
| 默认（不传后端参数） | `opengl` | 开发者本地真机 GL |
| `-DTGFX_USE_SWIFTSHADER=ON` | `opengl-swiftshader` | CI / 本地复现 |
| `-DTGFX_USE_VULKAN=ON` | `vulkan` | 开发者本地真机 VK |
| `-DTGFX_USE_VULKAN=ON -DTGFX_USE_SWIFTSHADER=ON` | `vulkan-swiftshader` | CI / 本地复现 |
| `-DTGFX_USE_METAL=ON` | `metal` | Mac 真机 |
| `-DTGFX_USE_D3D=ON` | `d3d` | Win（WARP 或真机） |

---

## 六、开发者工作流

无论是修改代码导致输出变化还是新增测试，流程统一：

```
1. 跑想验证的后端测试，确认截图效果正确
2. accept（无需参数，只更新 version.json）
3. push
4. CI 全量跑 4 个 job：
   - 所有 key：情况 C（version.json 已更新，cache 未更新）→ 跳过比较 → PASS
5. CI 全绿 → 合入 main
6. 合入后，CI 的 update_baseline 切 main 重新生成 .cache → 后续正式验证 MD5
```

### 开发者本地命令

现有 `TGFXFullTest` 拆分为各后端独立可执行文件（详见第八节），开发者可按需选择跑哪些后端，无需重新 cmake 切换：

```bash
# 跑测试（选你要验证的后端，可以跑一个或多个）
./build/TGFXFullTest_OpenGL
./build/TGFXFullTest_Vulkan
./build/TGFXFullTest_Metal          # Mac only
./build/TGFXFullTest_D3D            # Win only

# 确认截图正确后，accept（不需要指定后端）
./accept_baseline.sh

# 提交
git add test/baseline/version.json
git commit -m "Update baseline."
git push
```

### 典型场景

**Mac 开发者改了共享代码**：跑 GL + Metal 确认效果 → `./accept_baseline.sh` → push → CI 全绿 → 合入。

**Win 开发者改了共享代码**：跑 GL + VK + D3D 确认效果 → `./accept_baseline.sh` → push → CI 全绿 → 合入。

---

## 七、关键技术决策

| 决策 | 理由 |
|---|---|
| 保留 MD5 精确匹配，不引入 fuzzy tolerance | SwiftShader/WARP 是确定性渲染器，精确匹配可靠；Metal 偶尔换代时手动 re-accept 成本可控 |
| `CompareVersionAndMd5` 核心逻辑不改 | 降低风险，复用经过验证的判定机制（情况 A/B/C/D）；情况 C 天然兜底新增测试和跨平台场景 |
| 后端不在 main 上时不切 main 生成 cache | 直接在当前分支生成，基准 = 上次 accept 的状态 |
| PR 强制全量 CI | 杜绝漏检，4 job 并行时间可接受 |
| version.json 所有后端共用一份 | 只记录 key→commit，与后端无关；只有 .cache/md5.json 按后端隔离 |

---

## 八、测试目标拆分

现有 `TGFXFullTest` 拆分为各后端独立的可执行文件：

```
TGFXFullTest_OpenGL
TGFXFullTest_Vulkan
TGFXFullTest_Metal      ← 仅 Mac 编译
TGFXFullTest_D3D        ← 仅 Win 编译
```

**好处**：
- 各后端独立编译，不需要重新 cmake 切换后端
- 可执行文件名即后端，不会搞混
- 本地可同时保留多个后端的 build 目录并行开发
- CI 中每个 job 直接跑对应目标

**CMake 实现**：

`TGFX_BACKEND_NAME` 由编译选项自动决定，包含渲染器后缀。Target 按图形 API 拆分为 4 个，后缀自动追加：

```cmake
# 根据渲染器自动决定后缀
if (TGFX_USE_SWIFTSHADER)
    set(BACKEND_SUFFIX "-swiftshader")
else ()
    set(BACKEND_SUFFIX "")
endif ()

# OpenGL target
add_executable(TGFXFullTest_OpenGL ${TEST_SOURCES} test/src/opengl/DevicePool.cpp)
target_compile_definitions(TGFXFullTest_OpenGL PRIVATE TGFX_BACKEND_NAME="opengl${BACKEND_SUFFIX}")

# Vulkan target
add_executable(TGFXFullTest_Vulkan ${TEST_SOURCES} test/src/vulkan/DevicePool.cpp)
target_compile_definitions(TGFXFullTest_Vulkan PRIVATE TGFX_BACKEND_NAME="vulkan${BACKEND_SUFFIX}")

# Metal target (Mac only)
if (APPLE)
    add_executable(TGFXFullTest_Metal ${TEST_SOURCES} test/src/metal/DevicePool.mm)
    target_compile_definitions(TGFXFullTest_Metal PRIVATE TGFX_BACKEND_NAME="metal")
endif ()

# D3D target (Win only)
if (WIN32)
    add_executable(TGFXFullTest_D3D ${TEST_SOURCES} test/src/d3d/DevicePool.cpp)
    target_compile_definitions(TGFXFullTest_D3D PRIVATE TGFX_BACKEND_NAME="d3d")
endif ()
```

**效果**：
- 开发者本地不传 SwiftShader → `TGFX_BACKEND_NAME="opengl"` → `.cache/opengl/`
- CI 传 `-DTGFX_USE_SWIFTSHADER=ON` → `TGFX_BACKEND_NAME="opengl-swiftshader"` → `.cache/opengl-swiftshader/`
- 同一个 target 名，不同 build 配置自动写入不同 cache 目录，互不干扰

**开发者本地使用**：

```bash
# 编译（不传 SwiftShader，用真机 GPU）
cmake -DTGFX_BUILD_TESTS=ON -B build
cmake --build build --target TGFXFullTest_OpenGL TGFXFullTest_Metal

# 跑测试
./build/TGFXFullTest_OpenGL
./build/TGFXFullTest_Metal
```

---

## 九、代码改动清单

| 文件 | 改动 |
|---|---|
| `test/src/utils/Baseline.cpp` | cache 路径改为 `.cache/ + TGFX_BACKEND_NAME + /` |
| `CMakeLists.txt` | 拆分 `TGFXFullTest` 为 4 个独立目标；`TGFX_BACKEND_NAME` 由后端+SwiftShader 选项自动决定 |
| `update_baseline.sh` | 去掉 VK→GL 回退；各后端独立生成 cache；不在 main 时跳过切 main |
| `accept_baseline.sh` | 简化为无参数，只更新 version.json（跑测试由各 `TGFXFullTest_{Backend}` 独立完成） |
| `.github/workflows/autotest.yml` | 4 job matrix（mac-opengl/metal + win-vulkan/d3d），各 job 编译时传 SwiftShader/WARP 参数 |

---

## 十、实施计划

| 阶段 | 内容 |
|---|---|
| 1 | `Baseline.cpp` + `CMakeLists.txt` 改动 |
| 2 | `update_baseline.sh` + `accept_baseline.sh` 重写 |
| 3 | CI yml 重写 + 首次 accept 全部后端生成 baseline |
| 4 | 验证全流程（4 后端 CI 全绿） |

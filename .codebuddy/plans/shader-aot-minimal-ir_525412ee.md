---
name: shader-aot-minimal-ir
overview: 实现最小类型化 AOT IR 系统：让每个内置 FragmentProcessor 降低为类型化 AOT 节点，再由确定性规则分解为有限 AOT Pass，最终完全消除运行时 Shader 拼接。分阶段推进：先恢复可编译基线，再建立 Processor Trait 与降低机制，然后实现最小原型验证单 Pass/多 Pass 路径。
todos:
  - id: remove-include
    content: 删除 shader_build_tool/main.cpp 中 TextureClipShader.h 的残留 include
    status: completed
  - id: verify-build
    content: 使用 [subagent:code-explorer] 确认无其他引用后执行编译验证
    status: completed
    dependencies:
      - remove-include
---

## 用户需求

基于已确认的架构方向，实现 TGFX Shader AOT 分解系统的**阶段 0：恢复可编译基线**。

## 产品概述

清理工作区中因 TextureClipShader 文件删除而留下的残留引用，使 `shader_build_tool` 能够正常编译。这是后续 AOT IR 开发的硬性前提——基线不能有编译错误。

## 核心特性

- 清理 `tools/shader_build_tool/main.cpp` 中已删除文件 `TextureClipShader.h` 的残留 `#include`
- 确认清理后编译通过（`cmake --build` 目标 `TGFXFullTest_OpenGL`）

## 技术栈

- 语言：C++17
- 构建系统：CMake + Ninja
- 编译验证命令：`./codeformat.sh 2>/dev/null; true && cmake -G Ninja -DTGFX_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug -B cmake-build-debug && cmake --build cmake-build-debug --target TGFXFullTest_OpenGL`

## 实现方案

### 策略

删除 `tools/shader_build_tool/main.cpp` 第 53 行对已删除头文件的引用。该文件 `src/gpu/shaders/level1/TextureClipShader.h` 已被有意删除（Git status 中 `deleted`），运行时 `PermutationMatcher.cpp` 中已无对 `TextureClipShader` 的任何引用，因此只需清理构建工具中的残留 include。

### 验证

确认没有其他源文件引用 `TextureClipShader`，以保证删除 include 后不会在其他位置产生编译错误。根据之前的代码探索：

- `src/gpu/PermutationMatcher.cpp` 不引用 TextureClipShader
- `tools/shader_build_tool/main.cpp:53` 是唯一残留引用点

## 实现细节

修改范围极小：仅删除一行 `#include`。不涉及逻辑变更、接口变更或架构变更。

## 架构设计

无架构变更。

## 目录结构

```
tools/shader_build_tool/
└── main.cpp  # [MODIFY] 删除第 53 行 #include "gpu/shaders/level1/TextureClipShader.h"
```

## Agent Extensions

### SubAgent

- **code-explorer**
- Purpose: 确认 TextureClipShader 在整个项目中除 main.cpp:53 外无其他引用
- Expected outcome: 验证删除该 include 后不会产生其他编译错误
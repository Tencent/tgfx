# WebGPU Readback 同步接口与异步接口的栈深度分析

## 摘要

在 WebGPU 后端中，GPU 到 CPU 的像素回读依赖 `GPUBuffer.mapAsync()`。该 API 在浏览器中只能以 Promise 形式异步完成，而 tgfx 当前公开接口中存在同步语义的 `Surface::readPixels()`、`Surface::getColor()` 以及基于它们的截图比较流程。为了在 Web 平台保留同步 C++ 接口，当前实现通过 Emscripten Asyncify 在 WebAssembly 内部挂起和恢复调用栈。

该设计能够保持接口兼容，但引入 `ASYNCIFY_STACK_SIZE` 风险：当 `await mapAsync()` 发生在深层 C++ 同步调用栈内部时，Asyncify 必须保存整条 WebAssembly 调用栈。单测环境会显著放大该问题；但如果 Web 是 tgfx 的主要运行环境，同步 readback 接口在截图导出、像素采样、图像分析等业务场景中也会长期存在风险。

本文系统说明问题、根因、同步与异步接口的堆栈差异、可选方案及推荐路线。

---

## 1. 问题定义

### 1.1 WebGPU readback 的平台约束

WebGPU 不提供同步 GPU readback API。浏览器标准模型要求通过 `GPUBuffer.mapAsync()` 等待 GPU 写入完成：

```javascript
await buffer.mapAsync(GPUMapMode.READ);
const pixels = buffer.getMappedRange();
```

该异步设计的原因包括：

1. GPU 命令执行与 JavaScript 主线程解耦。
2. GPU 进程、渲染进程和浏览器主线程之间可能存在跨线程或跨进程同步。
3. 浏览器不能长时间阻塞主线程，否则 Promise continuation、输入事件和渲染刷新都无法推进。
4. WebGPU 的安全模型不允许暴露同步阻塞式显存访问接口。

因此，WebGPU readback 的本质是异步操作。

### 1.2 tgfx 当前接口的同步语义

tgfx 当前核心 API 中存在同步 readback 调用：

```cpp
bool Surface::readPixels(const ImageInfo& dstInfo, void* dstPixels, int srcX, int srcY);
```

其语义是：函数返回时，目标像素已经写入 `dstPixels`。`Surface::getColor()`、`Baseline::Compare(surface, key)` 和大量测试代码均依赖该语义。

在 WebGPU 后端中，为了在不改变调用方代码的情况下实现该同步语义，当前实现使用 Asyncify 将 C++ 同步调用包装为可挂起和恢复的 WebAssembly 执行流。

### 1.3 直接症状

在 WebGPU 测试中，如果 `ASYNCIFY_STACK_SIZE` 设置为 64KB，`readPixels()` 相关测试可能出现运行时异常，例如函数签名错配、栈恢复失败或其他由 Asyncify 栈保存不足引发的问题。将其提高到 256KB 后，测试能够继续运行，说明问题与 Asyncify 栈快照容量相关。

---

## 2. 根本原因

### 2.1 Asyncify 保存的是挂起时的真实 WebAssembly 栈

Asyncify 的工作方式可以简化为：

1. WebAssembly 调用 JavaScript 异步函数。
2. JavaScript 函数执行到 `await`。
3. Asyncify 将当前 WebAssembly 活跃调用栈复制到专用缓冲区。
4. JavaScript Promise 完成后，Asyncify 从缓冲区恢复 WebAssembly 调用栈。
5. C++ 代码从挂起点继续执行。

因此，`ASYNCIFY_STACK_SIZE` 的需求由挂起时的真实栈深度决定，而不是由 `mapAsync()` 本身决定。

### 2.2 当前同步 readback 调用链

当前路径可以抽象为：

```text
JavaScript entry
  -> C++ exported function or RunAllTests()
    -> business code / GoogleTest
      -> Surface::readPixels()
        -> Surface::asyncReadPixels()
          -> RenderContext::flush()
          -> DrawingManager::addTransferPixelsTask()
        -> SurfaceReadback::lockPixels()
          -> Context::flushAndSubmit(true)
          -> GPUBuffer::requestMapAsync()
            -> WebGPUBuffer::requestMapAsync()
              -> webgpu_buffer_map_sync()
                -> await GPUBuffer.mapAsync()
```

真正的挂起点在 `webgpu_buffer_map_sync()` 内部。当它执行 `await buffer.mapAsync()` 时，`Surface::readPixels()` 及其所有上层调用者都尚未返回。Asyncify 必须保存整条未返回的 C++ / WebAssembly 栈。

### 2.3 单测为什么更容易暴露

Google Test 和截图基准测试会显著加深调用栈：

```text
RunAllTests()
  -> RUN_ALL_TESTS()
    -> GoogleTest suite runner
      -> GoogleTest test dispatch
        -> TEST body
          -> Baseline::Compare()
            -> Surface::readPixels()
              -> WebGPU readback
                -> await mapAsync()
```

单测环境还具有以下放大因素：

1. Debug 构建通常使用 `-O0`，函数内联少，栈帧更多。
2. `ASSERTIONS=2` 增加检查逻辑和调用层级。
3. 截图测试高频调用 `readPixels()`。
4. Google Test 宏、fixture、断言和失败报告机制增加额外栈帧。

因此，单测是最坏情况压力测试。但该问题不是单测独有。

### 2.4 Web 业务中的常见触发场景

如果 Web 是 tgfx 的主要使用环境，以下业务场景仍然可能触发深栈同步 readback：

| 场景 | 触发接口 | 风险 |
|---|---|---|
| 截图导出 | `Surface::readPixels()` | 中到高 |
| 像素取色 | `Surface::getColor()` | 中 |
| 图像分析 | `Surface::readPixels()` | 高 |
| 视觉回归 | `Baseline::Compare()` 或自定义 compare | 高 |
| 调试工具 | 同步 readback | 中 |
| 每帧 readback | 同步 readback | 很高 |

普通渲染上屏不涉及 GPU 到 CPU readback，因此风险较低。问题集中在 GPU readback，而不是 WebGPU 渲染本身。

---

## 3. 不改造接口时的堆栈模型

### 3.1 使用方式

调用方保持同步风格：

```cpp
surface->readPixels(info, pixels);
```

或者 JavaScript 调用一个 C++ 导出函数：

```javascript
Module._ExportImage();
```

C++ 内部继续使用同步 readback：

```cpp
bool ExportImage(Surface* surface, const ImageInfo& info, void* pixels) {
  if (!surface->readPixels(info, pixels)) {
    return false;
  }
  return EncodeImage(info, pixels);
}
```

### 3.2 业务环境中的典型栈

```text
JS click / app callback
  -> embind / ccall
    -> C++ business function
      -> rendering or export pipeline
        -> Surface::readPixels()
          -> Surface::asyncReadPixels()
          -> SurfaceReadback::lockPixels()
            -> Context::flushAndSubmit(true)
            -> WebGPUBuffer::requestMapAsync()
              -> webgpu_buffer_map_sync()
                -> await GPUBuffer.mapAsync()
```

### 3.3 Asyncify 保存范围

在 `await mapAsync()` 发生时，以下栈帧均未返回：

```text
C++ business function
  + rendering / export pipeline
  + Surface::readPixels()
  + Surface::asyncReadPixels()
  + SurfaceReadback::lockPixels()
  + Context::flushAndSubmit()
  + WebGPUBuffer::requestMapAsync()
  + EM_ASYNC_JS bridge
```

如果在 Google Test 中，还要叠加：

```text
RunAllTests()
  + RUN_ALL_TESTS()
  + GoogleTest runner
  + TEST body
  + Baseline::Compare()
```

### 3.4 栈深度评估

| 环境 | 典型 WebAssembly / C++ 栈深度 | `ASYNCIFY_STACK_SIZE` 风险 |
|---|---:|---|
| 简单 JS 导出函数直接调用 `readPixels()` | 6 到 10 层 | 中 |
| 业务导出图片或图像处理流程 | 10 到 18 层 | 中到高 |
| Google Test + Baseline 截图比较 | 20 层以上 | 高 |
| Debug + `ASSERTIONS=2` + 截图测试 | 20 层以上且栈帧更大 | 很高 |

这些层级不是精确函数数，而是风险等级估算。实际容量还受编译优化级别、函数内联、局部变量大小和工具链版本影响。

### 3.5 特征总结

不改造接口时：

```text
await 发生在 C++ 同步 API 内部
=> C++ 栈尚未返回
=> Asyncify 保存整条业务栈
=> 栈需求随业务复杂度增长
```

因此，保留同步 readback 作为主路径，会长期依赖较大的 `ASYNCIFY_STACK_SIZE` 安全余量。

---

## 4. 改造为 Web 异步接口时的堆栈模型

### 4.1 使用方式

Web 调用方改为 Promise 风格：

```javascript
const pixels = await surface.readPixelsAsync(x, y, width, height);
```

或者 C++ 层显式拆分 readback 生命周期：

```cpp
auto readback = surface->asyncReadPixels(rect);
readback->requestMapAsync(context);

if (readback->isReady(context)) {
  auto pixels = readback->lockMappedPixels(context);
  readback->unlockPixels(context);
}
```

### 4.2 分阶段执行模型

异步接口的关键是将 readback 拆成三个阶段。

#### 阶段一：启动 readback

```text
JS
  -> surface.readPixelsAsync()
    -> C++ Surface::asyncReadPixels()
      -> submit copyTextureToBuffer
      -> start mapAsync request
    <- return Promise
```

此阶段不等待 Promise。C++ 栈在函数返回时清空。

#### 阶段二：JavaScript 浅层等待

```text
JS
  -> await GPUBuffer.mapAsync()
```

此时没有深层 C++ 调用栈。等待发生在 JavaScript Promise 层，而不是 `Surface::readPixels()` 内部。

#### 阶段三：完成拷贝和返回结果

```text
Promise resolved
  -> C++ lock already mapped buffer
  -> CopyPixels
  -> return typed array or pixel buffer
```

此阶段读取的是已经 ready 的 buffer，不再触发 `await`。

### 4.3 Asyncify 保存范围

理想 Promise 化实现中，`await` 发生在 JavaScript 层，此时 WebAssembly 已经返回。因此 Asyncify 保存范围为：

```text
none
```

如果实现中仍使用一个很浅的 `EM_ASYNC_JS` 包装，保存范围也只有：

```text
readPixelsAsync bridge
  -> await mapAsync
```

不再包含业务 C++ 栈、`Surface::readPixels()`、`Baseline::Compare()` 或 Google Test 调度栈。

### 4.4 栈深度评估

| 环境 | 典型 WebAssembly / C++ 栈深度 | `ASYNCIFY_STACK_SIZE` 风险 |
|---|---:|---|
| JS Promise 层直接等待 `mapAsync()` | 0 层 | 很低 |
| 浅层 `EM_ASYNC_JS` bridge 内等待 | 1 到 3 层 | 低 |
| C++ 显式 async readback + JS scheduler | 0 到 3 层 | 低 |
| Google Test 若仍同步 `readPixels()` | 20 层以上 | 高 |
| Google Test 若改为异步 helper | 0 到 5 层 | 低到中 |

### 4.5 特征总结

改造接口后：

```text
await 发生在 JS Promise 层
=> C++ 栈已经返回
=> Asyncify 不保存业务深栈
=> ASYNCIFY_STACK_SIZE 压力基本消失
```

这不是减少几层函数调用，而是把保存范围从“整条业务栈”降为“空栈或极浅桥接栈”。

---

## 5. 三类方案对比

### 5.1 方案 A：保持同步接口，增大 `ASYNCIFY_STACK_SIZE`

#### 描述

继续使用现有同步接口，WebGPU 内部通过 Asyncify 实现同步等待。测试构建使用较大的 `ASYNCIFY_STACK_SIZE`，例如 256KB。

#### 优点

1. 对公开 API 无影响。
2. 对调用方无影响。
3. 改动最小。
4. 能快速保证 Google Test 与现有截图测试稳定运行。

#### 缺点

1. 没有改变深层挂起结构。
2. `ASYNCIFY_STACK_SIZE` 需要为最坏栈深度预留空间。
3. Web 业务如果频繁同步 readback，仍可能遇到类似问题。
4. 与 Web 平台的 Promise 模型不一致。

#### 适用场景

- 短期兼容。
- 单测稳定性。
- 低频截图导出。
- 调试和迁移阶段。

### 5.2 方案 B：保持公开 API，不改变调用方式，仅做内部浅层重排

#### 描述

仍保留 `Surface::readPixels()` 同步接口，但在 WebGPU 后端中增加更短路径，使 `await mapAsync()` 尽量靠近 `Surface::readPixels()` 内部，而不是埋在 `SurfaceReadback::lockPixels()` 和 `WebGPUBuffer::requestMapAsync()` 深层。

#### 栈模型

改造前：

```text
Surface::readPixels()
  -> SurfaceReadback::lockPixels()
    -> WebGPUBuffer::requestMapAsync()
      -> await mapAsync()
```

改造后：

```text
Surface::readPixels()
  -> WebGPU shallow readback path
    -> await mapAsync()
```

#### 优点

1. 公开 API 不变。
2. 调用方式不变。
3. 可以减少若干通用 readback 层级。
4. 实现复杂度明显低于完整异步接口改造。

#### 缺点

1. `await` 仍发生在 `Surface::readPixels()` 返回之前。
2. 业务调用栈、Google Test 栈仍必须保存。
3. 只能缓解，不能根治 `ASYNCIFY_STACK_SIZE` 问题。
4. 不一定能从 256KB 稳定降回 64KB。

#### 适用场景

- 希望保持接口完全兼容。
- 希望减少一部分 Asyncify 栈压力。
- 尚不能推动 Web API Promise 化。

### 5.3 方案 C：新增 Web 异步 readback 接口

#### 描述

保留 `Surface::readPixels()` 作为兼容接口，同时新增 Web 推荐的异步接口，例如：

```javascript
const pixels = await surface.readPixelsAsync(x, y, width, height);
```

或 C++ 层显式异步对象：

```cpp
auto readback = surface->asyncReadPixels(rect);
readback->requestMapAsync(context);
```

#### 优点

1. 符合 WebGPU 和浏览器 Promise 模型。
2. `await` 不再发生在深层 C++ 同步栈内部。
3. 显著降低或消除 `ASYNCIFY_STACK_SIZE` 压力。
4. 适合 Web 作为主要运行环境的长期演进。
5. 可逐步迁移，不需要立即删除同步接口。

#### 缺点

1. 需要新增 API 或 JS 封装。
2. 调用方需要使用 `await`。
3. 测试框架如果要受益，也需要使用异步 helper 或 Web 专用测试路径。
4. 生命周期管理需要更清晰，例如 readback 对象、mapped buffer 和像素数据所有权。

#### 适用场景

- Web 是主要使用环境。
- 截图导出、取色、图像分析等 readback 是常规能力。
- 希望长期降低 Asyncify 依赖。
- 希望提供符合 Web 习惯的 tgfx API。

---

## 6. 栈深度差异总表

| 维度 | 不改接口：同步 `readPixels()` | 内部浅层重排 | 改造接口：`readPixelsAsync()` |
|---|---|---|---|
| 调用方式 | 不变 | 不变 | 改为 Promise 或显式异步对象 |
| `await mapAsync()` 位置 | 深层 C++ 内部 | `Surface::readPixels()` 较浅内部 | JavaScript Promise 层 |
| C++ 栈是否已返回 | 否 | 否 | 是 |
| Asyncify 保存范围 | 整条业务栈 | 业务栈 + 较少内部层 | 空栈或极浅 bridge |
| 典型业务栈深度 | 8 到 18 层 | 6 到 15 层 | 0 到 3 层 |
| 单测栈深度 | 20 层以上 | 仍接近 20 层 | 使用异步 helper 后显著降低 |
| 是否能根治 stack size | 不能 | 不能，只能缓解 | 可以从结构上解决 |
| API 影响 | 无 | 无 | 新增接口，推荐 Web 使用 |
| 实现复杂度 | 低 | 中 | 中到高 |
| 长期 Web 适配性 | 一般 | 一般 | 最好 |

---

## 7. 推荐路线

### 7.1 短期：同步接口作为兼容路径

短期不建议立刻移除或大规模重构 `Surface::readPixels()`。应保持同步接口可用，并为测试构建保留足够的 `ASYNCIFY_STACK_SIZE`。该路径服务于：

1. 现有 C++ API 兼容。
2. 当前 Google Test 和 Baseline 测试。
3. 低频调试和迁移场景。

建议明确文档化：WebGPU 后端支持同步 readback，但它依赖 Asyncify，存在栈快照成本，不应作为高频 Web 业务主路径。

### 7.2 中期：为 Web 新增 Promise 化 readback

如果 Web 是 tgfx 的主要使用环境，建议新增 Web 友好接口：

```javascript
const pixels = await surface.readPixelsAsync(x, y, width, height);
```

内部实现原则：

```text
C++ start readback
  -> submit copy command
  -> return pending operation
JS await mapAsync
  -> no C++ deep stack alive
C++ finish readback
  -> lock mapped data
  -> copy or expose typed array
```

同步接口继续存在，但推荐 Web 业务使用异步接口。

### 7.3 长期：迁移高频 readback 场景

长期应优先迁移以下路径：

1. Web 截图导出。
2. Web 取色器。
3. Web 图像分析。
4. Web 视觉回归测试。
5. 任何每帧或高频 readback。

迁移后，`ASYNCIFY_STACK_SIZE` 不再需要覆盖这些业务深栈，只需要覆盖遗留同步兼容路径。

---

## 8. 结论

WebGPU readback 的根本矛盾是：Web 平台只提供异步 `mapAsync()`，而 tgfx 现有核心 API 中存在同步 readback 语义。若不改造接口，`await mapAsync()` 发生在 `Surface::readPixels()` 深层，Asyncify 必须保存完整业务调用栈；这在 Google Test 中最容易暴露，但在 Web 截图、取色和图像分析等业务中也可能长期出现。

不改接口的方案可以保证兼容，但无法根治 `ASYNCIFY_STACK_SIZE` 问题。内部浅层重排只能减少少量栈帧。真正具有量级收益的是新增 Web 异步 readback 接口，使 `await` 发生在 JavaScript Promise 层，让 C++ / WebAssembly 深栈在等待前已经返回。

因此，推荐策略是：同步 `Surface::readPixels()` 保留为兼容路径，Web 主要使用场景逐步引入并推荐 `readPixelsAsync()`。这样既能保持 tgfx 既有 API 稳定，又能让 WebGPU readback 符合浏览器异步模型，从结构上降低 Asyncify 栈保存压力。

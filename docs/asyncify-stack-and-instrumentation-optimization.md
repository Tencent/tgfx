# Asyncify 栈保存与插桩范围的系统优化方案

## 摘要

tgfx WebGPU 后端在浏览器中运行时，需要将 WebGPU 的异步 `GPUBuffer.mapAsync()` 映射到既有 C++ 同步接口 `Surface::readPixels()`。当前实现通过 Emscripten Asyncify 在 `EM_ASYNC_JS` 边界挂起 WebAssembly 调用栈，并在 JavaScript Promise 完成后恢复执行。该方案能够保持 C++ API 的同步语义，但引入了两个相互独立的优化维度：

1. **插桩范围优化**：控制哪些 WebAssembly 函数被 Asyncify 变换，主要影响二进制体积、编译时间和非异步路径运行开销。
2. **栈保存容量优化**：控制一次挂起时可保存的调用栈大小，主要影响运行时正确性和单次挂起的内存占用。

本文建立二者的形式化模型，分析 `ASYNCIFY_IMPORTS`、`ASYNCIFY_REMOVE` 与 `ASYNCIFY_STACK_SIZE` 的作用边界，并提出分阶段、可验证、低风险的优化路径。

---

## 1. 背景与问题定义

### 1.1 WebGPU readback 的异步约束

WebGPU 不提供同步 readback API。典型 readback 流程为：

```javascript
await buffer.mapAsync(GPUMapMode.READ);
const data = buffer.getMappedRange();
```

`mapAsync()` 返回 Promise，其完成时刻依赖 GPU 命令执行、进程间同步和浏览器调度。若在 JavaScript 主线程上采用 busy-wait 等待 Promise，事件循环无法继续推进，Promise continuation 也无法执行，系统会形成逻辑死锁。因此，WebGPU readback 必须以真正让出事件循环的方式等待。

### 1.2 tgfx 的同步 API 约束

tgfx 既有 API 将 readback 表达为同步函数：

```cpp
bool Surface::readPixels(const ImageInfo& dstInfo, void* dstPixels, int srcX, int srcY);
```

调用方假设函数返回时像素数据已经写入 `dstPixels`。在 Google Test 截图比较、像素断言、`Surface::getColor()` 等路径中，该同步语义被广泛依赖。若要求调用方整体改写为异步接口，影响范围将从 WebGPU 后端扩散至核心 API、测试框架和业务调用层。

因此，当前阶段采用 Asyncify 维持同步语义是合理的工程折中。

---

## 2. 当前实现的控制流模型

WebGPU readback 的关键调用链可抽象为：

```text
GoogleTest / user entry
  -> Surface::readPixels()
    -> Surface::asyncReadPixels()
      -> RenderContext::flush()
      -> DrawingManager::addTransferPixelsTask()
    -> SurfaceReadback::lockPixels()
      -> Context::flushAndSubmit(true)
      -> WebGPUBuffer::requestMapAsync()
        -> webgpu_buffer_map_sync()
          -> await GPUBuffer.mapAsync()
```

其中 `webgpu_buffer_map_sync()` 是 `EM_ASYNC_JS` 函数。Asyncify 在该函数内部遇到 `await` 时挂起 WebAssembly 执行，并保存从挂起点回溯到当前 WebAssembly 栈底的所有活跃帧。

该模型揭示了一个关键事实：

> `ASYNCIFY_IMPORTS` 决定哪些函数可能被插桩；`ASYNCIFY_STACK_SIZE` 决定实际挂起时能保存多大的调用栈。二者优化对象不同，不能互相替代。

---

## 3. 维度一：插桩范围优化

### 3.1 Asyncify 插桩的含义

Asyncify 不是简单地在某个 JavaScript 函数处暂停。为了保证暂停后能够恢复 C/C++ 控制流，Binaryen 需要改写可能位于异步调用链上的 WebAssembly 函数，在函数入口、出口和调用点插入状态检查、栈保存与栈恢复逻辑。

设 WebAssembly 函数集合为 \(F\)，异步导入函数集合为 \(A\)，调用图为 \(G=(F,E)\)。Asyncify 需要插桩的函数集合近似为：

\[
I = \{ f \in F \mid f \leadsto a, a \in A \}
\]

即所有可能直接或间接调用异步导入函数的函数。

### 3.2 `ASYNCIFY_IMPORTS` 的作用

`ASYNCIFY_IMPORTS` 用于声明哪些外部 JavaScript 函数可能触发异步挂起。例如：

```cmake
-sASYNCIFY_IMPORTS=['webgpu_buffer_map_sync']
```

它的主要收益是缩小 \(A\)，进而缩小插桩集合 \(I\)。该优化影响如下：

| 指标 | 影响 | 原因 |
|---|---|---|
| wasm 体积 | 可能降低 | 减少无关函数中的 Asyncify 状态机代码 |
| 编译和链接时间 | 可能降低 | Binaryen 需要分析和改写的函数减少 |
| 非 readback 路径性能 | 可能提升 | 无关函数不再执行 Asyncify 状态检查 |
| 实际挂起所需栈容量 | 无直接降低 | 挂起时仍需保存当前真实调用栈 |

因此，`ASYNCIFY_IMPORTS` 是插桩范围优化工具，而非栈内存优化工具。

### 3.3 `ASYNCIFY_REMOVE` 的作用

`ASYNCIFY_REMOVE` 用于显式排除某些符号或符号模式。例如当前配置排除了 shaderc、glslang、Tint、SPIR-V Tools 等纯 CPU shader 编译路径。其目的包括：

1. 避免对确定不会参与异步等待的计算密集型库插桩。
2. 降低 wasm 体积和编译时间。
3. 避免第三方库复杂控制流在 Asyncify 变换下暴露工具链兼容问题。

`ASYNCIFY_REMOVE` 与 `ASYNCIFY_IMPORTS` 可以同时使用。前者是负向裁剪，后者是正向声明。二者共同服务于插桩范围控制。

### 3.4 插桩范围优化的局限

插桩范围优化不改变实际 C++ 调用链。若挂起点仍位于 `Surface::readPixels()` 深层，则一次挂起必须保存的活跃帧仍然包含：

- Google Test 调度栈
- tgfx 测试宏包装栈
- Surface readback 栈
- Context flush / submit 栈
- GPUBuffer map 栈
- EM_ASYNC_JS 边界栈

因此，即使 `ASYNCIFY_IMPORTS` 将插桩集合缩到最小，`ASYNCIFY_STACK_SIZE` 仍可能需要保持较大数值。

---

## 4. 维度二：栈保存容量优化

### 4.1 `ASYNCIFY_STACK_SIZE` 的语义

`ASYNCIFY_STACK_SIZE` 是 Asyncify 用于保存 WebAssembly 调用栈快照的缓冲区大小。一次挂起时，Asyncify 需要将活跃调用帧复制到该缓冲区；恢复时再从缓冲区还原。

设一次挂起时活跃调用帧集合为 \(S\)，每个帧需要保存的字节数为 \(b_i\)，则所需容量为：

\[
B = \sum_{i \in S} b_i + \epsilon
\]

其中 \(\epsilon\) 表示 Asyncify 元数据、对齐和工具链内部开销。

若 `ASYNCIFY_STACK_SIZE < B`，运行时可能出现栈保存失败、函数签名错配、恢复状态损坏或其他难以定位的异常。

### 4.2 为什么增大栈只是局部缓解

将 `ASYNCIFY_STACK_SIZE` 从 64KB 提高到 256KB 能够覆盖当前测试中的深调用链，因此短期内可以解除失败。但该方法有三个问题：

1. **缺乏结构性保证**：未来测试框架、编译选项或内联策略变化都可能改变栈深度。
2. **扩大固定内存预算**：每个实例需要保留更大的 Asyncify 栈保存区。
3. **掩盖架构问题**：真正的问题是异步挂起点位于深层同步 API 内部。

因此，增大 `ASYNCIFY_STACK_SIZE` 是兼容性修复，不是系统性优化。

### 4.3 降低栈需求的唯一根本方向

要降低 \(B\)，必须减少挂起时的活跃调用帧集合 \(S\)，即将 `await` 边界移动到更浅的位置。

可选策略包括：

1. **保持当前同步 API，在内部继续使用 Asyncify**  
   实现成本最低，但需要较大的 `ASYNCIFY_STACK_SIZE`。

2. **将异步边界上移到测试或应用入口**  
   例如测试入口 `RunTest()` / `RunAllTests()` 变为异步调度器，测试内部提交 readback 请求后由外层统一 await。此时挂起栈更浅，`ASYNCIFY_STACK_SIZE` 可显著降低。

3. **引入显式异步 readback API**  
   例如 `Surface::asyncReadPixels()` 返回 readback handle，调用方通过轮询或回调获取结果。该方案最符合 WebGPU 模型，但需要更大范围 API 改造。

4. **使用 JSPI 替代 Asyncify**  
   JSPI 由运行时管理 wasm 与 Promise 的挂起恢复，不依赖 Asyncify 栈复制模型。但其可用性、浏览器覆盖率、Emscripten 入口声明、间接调用兼容性需要独立验证。

---

## 5. 推荐优化路线

### 5.1 第一阶段：稳定当前实现

目标是在不改变 tgfx API 的前提下，使当前 WebGPU 测试稳定运行。

建议配置：

```cmake
-sASYNCIFY=1
-sASYNCIFY_STACK_SIZE=262144
-sASYNCIFY_IMPORTS=['webgpu_buffer_map_sync']
-sASYNCIFY_REMOVE=['shaderc*','glslang*','spv::*','spvtools*','tint::*','Glslang*','TShader*','Compile*ToSpv*','SpvCompilat*']
```

第一阶段的性质是工程稳定性修复。`ASYNCIFY_STACK_SIZE=262144` 用于保证当前深调用链安全；`ASYNCIFY_IMPORTS` 与 `ASYNCIFY_REMOVE` 用于降低非相关代码的插桩成本。

验收标准：

1. WebGPU 全量测试不再出现 Asyncify 栈不足或函数签名恢复异常。
2. wasm 体积相较未裁剪插桩版本下降或不增长。
3. shader 编译相关路径不因 Asyncify 插桩产生行为差异。

### 5.2 第二阶段：量化插桩收益

应建立可重复的测量流程，而非依赖经验判断。建议比较以下构建配置：

| 配置 | `ASYNCIFY_IMPORTS` | `ASYNCIFY_REMOVE` | `ASYNCIFY_STACK_SIZE` |
|---|---|---|---|
| Baseline | 未指定 | 未指定 | 262144 |
| Remove-only | 未指定 | 启用 | 262144 |
| Imports-only | 启用 | 未指定 | 262144 |
| Imports+Remove | 启用 | 启用 | 262144 |

记录指标：

- wasm 文件大小
- js glue 文件大小
- link 时间
- 首次加载时间
- 全量测试耗时
- readback 密集测试耗时

该阶段用于回答“插桩范围优化实际收益多少”，但不应期望其显著降低 `ASYNCIFY_STACK_SIZE`。

### 5.3 第三阶段：上移异步边界

若希望系统性降低 `ASYNCIFY_STACK_SIZE`，应将异步等待从 `WebGPUBuffer::requestMapAsync()` 深层调用移动到更浅层。这里需要特别区分两类改造：

1. **仅把 `RunAllTests()` 包装成 JavaScript async 函数是不充分的。** 如果 `Surface::readPixels()` 内部仍然执行 `await buffer.mapAsync()`，Asyncify 仍然必须保存 `RUN_ALL_TESTS()` 到 `WebGPUBuffer::requestMapAsync()` 的完整调用栈。
2. **真正有效的改造是让深层 C++ API 不再等待 Promise。** 深层 API 只提交 readback 请求并返回一个可轮询对象，等待动作由更浅层的 WebGPU 调度器或测试调度器统一执行。

#### 5.3.1 当前同步实现的关键问题

当前 `SurfaceReadback::lockPixels()` 在深层同步等待 readback 完成：

```cpp
const void* SurfaceReadback::lockPixels(Context* context, bool flipY) {
  auto readbackBuffer = proxy->getBuffer();
  if (readbackBuffer == nullptr) {
    context->flushAndSubmit(true);
    readbackBuffer = proxy->getBuffer();
  } else {
    context->gpu()->queue()->waitUntilCompleted();
  }

  auto gpuBuffer = readbackBuffer->gpuBuffer();
  if (!gpuBuffer->isReady()) {
    gpuBuffer->requestMapAsync();
    if (!gpuBuffer->isReady()) {
      return nullptr;
    }
  }
  return gpuBuffer->map();
}
```

在 WebGPU 后端中，`gpuBuffer->requestMapAsync()` 最终进入：

```cpp
EM_ASYNC_JS(int, webgpu_buffer_map_sync, (WGPUBuffer buffer, size_t size), {
  var bufferWrapper = WebGPU.mgrBuffer.objects[buffer];
  if (!bufferWrapper) {
    return 1;
  }
  await bufferWrapper.object.mapAsync(1, 0, size);
  bufferWrapper.onUnmap = bufferWrapper.onUnmap || [];
  return 0;
});
```

因此，真正的挂起点位于 `Surface::readPixels()` 的深层实现中。只要这个结构不变，Asyncify 保存的就是完整深栈。

#### 5.3.2 目标一：将 readback 拆成 request / poll / lock 三段

第一步是在 `SurfaceReadback` 层引入显式异步生命周期。新的 API 不改变 GPU 后端抽象，只把“启动异步 map”和“读取已经 map 完成的数据”拆开。

建议接口形态如下：

```cpp
class SurfaceReadback {
 public:
  bool requestMapAsync(Context* context);

  bool isReady(Context* context) const;

  const void* lockMappedPixels(Context* context, bool flipY = false);

  void unlockPixels(Context* context);
};
```

对应实现可以写成：

```cpp
bool SurfaceReadback::requestMapAsync(Context* context) {
  if (context != proxy->getContext()) {
    return false;
  }

  auto readbackBuffer = proxy->getBuffer();
  if (readbackBuffer == nullptr) {
    context->flushAndSubmit(false);
    readbackBuffer = proxy->getBuffer();
    if (readbackBuffer == nullptr) {
      return false;
    }
  }

  auto gpuBuffer = readbackBuffer->gpuBuffer();
  if (!gpuBuffer->isReady()) {
    gpuBuffer->requestMapAsync();
  }
  return true;
}

const void* SurfaceReadback::lockMappedPixels(Context* context, bool flipY) {
  if (!isReady(context)) {
    return nullptr;
  }

  auto readbackBuffer = proxy->getBuffer();
  auto pixels = readbackBuffer->gpuBuffer()->map();
  if (pixels == nullptr) {
    return nullptr;
  }

  if (flipY) {
    flipYPixels = malloc(_info.byteSize());
    if (flipYPixels == nullptr) {
      readbackBuffer->gpuBuffer()->unmap();
      return nullptr;
    }
    CopyPixels(_info, pixels, _info, flipYPixels, true);
    pixels = flipYPixels;
  }

  locked = true;
  return pixels;
}
```

此时深层 C++ 代码不再执行 `await`，而是只做三件事：提交 GPU copy、启动 `mapAsync()`、返回控制权。`mapAsync()` 的完成由事件循环推进，调用方通过 `isReady()` 判断结果是否可读。

#### 5.3.3 目标二：增加浅层 WebGPU readback 调度器

为了避免每个测试或业务调用方自行管理 readback，可以建立一个 WebGPU 专用调度器。调度器持有 pending readback 列表，并在浅层统一等待。

建议抽象如下：

```cpp
class AsyncReadbackTask {
 public:
  virtual ~AsyncReadbackTask() = default;

  virtual bool begin(Context* context) = 0;

  virtual bool isReady(Context* context) const = 0;

  virtual bool finish(Context* context) = 0;
};

class WebGPUReadbackScheduler {
 public:
  static WebGPUReadbackScheduler* Get();

  void add(std::unique_ptr<AsyncReadbackTask> task);

  bool beginAll(Context* context);

  bool isAllReady(Context* context) const;

  bool finishAll(Context* context);

 private:
  std::vector<std::unique_ptr<AsyncReadbackTask>> tasks = {};
};
```

一个 surface readback task 可以实现为：

```cpp
class SurfaceReadbackTask : public AsyncReadbackTask {
 public:
  SurfaceReadbackTask(std::shared_ptr<SurfaceReadback> readback, Pixmap dst)
      : readback(std::move(readback)), dst(dst) {
  }

  bool begin(Context* context) override {
    return readback != nullptr && readback->requestMapAsync(context);
  }

  bool isReady(Context* context) const override {
    return readback != nullptr && readback->isReady(context);
  }

  bool finish(Context* context) override {
    auto pixels = readback->lockMappedPixels(context);
    if (pixels == nullptr) {
      return false;
    }
    CopyPixels(readback->info(), pixels, dst.info(), dst.writablePixels());
    readback->unlockPixels(context);
    return true;
  }

 private:
  std::shared_ptr<SurfaceReadback> readback = nullptr;
  Pixmap dst = {};
};
```

该结构的关键点是：`begin()` 会触发 `mapAsync()`，但不会同步等待；`finish()` 只在 `isReady()` 已经为 true 后执行，因此也不会触发 Asyncify 挂起。

#### 5.3.4 目标三：在测试入口上移等待点

Web 端测试入口目前是同步导出函数：

```cpp
extern "C" {

EMSCRIPTEN_KEEPALIVE
int RunAllTests() {
  initTests();
  return RUN_ALL_TESTS();
}

}
```

为了让浏览器事件循环有机会推进 pending Promise，需要增加浅层异步入口。该入口不应再让 `RUN_ALL_TESTS()` 内部执行深层 `await`，而应执行“启动测试逻辑、收集 readback、等待 ready、完成验证”的分段流程。

需要注意：Google Test 本身不是异步测试框架，不能在一个普通 `TEST()` 函数中返回到 JavaScript 后再自动恢复原测试函数。因此下列代码是测试入口分段模型，实际落地时需要配合 WebGPU 专用测试 helper：测试体只注册 pending readback 和验证任务，不在 `TEST()` 内立即读取像素并断言。

概念代码如下：

```cpp
extern "C" {

EMSCRIPTEN_KEEPALIVE
int RunAllTestsBegin() {
  initTests();
  return RUN_ALL_TESTS();
}

EMSCRIPTEN_KEEPALIVE
int RunPendingReadbacksBegin(Context* context) {
  return WebGPUReadbackScheduler::Get()->beginAll(context) ? 0 : 1;
}

EMSCRIPTEN_KEEPALIVE
int RunPendingReadbacksReady(Context* context) {
  return WebGPUReadbackScheduler::Get()->isAllReady(context) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
int RunPendingReadbacksFinish(Context* context) {
  return WebGPUReadbackScheduler::Get()->finishAll(context) ? 0 : 1;
}

}
```

JavaScript 侧浅层调度可以写成：

```javascript
async function runWebGPUTests(module, contextHandle) {
  const testResult = module._RunAllTestsBegin();
  if (testResult !== 0) {
    return testResult;
  }

  const beginResult = module._RunPendingReadbacksBegin(contextHandle);
  if (beginResult !== 0) {
    return beginResult;
  }

  while (module._RunPendingReadbacksReady(contextHandle) === 0) {
    await new Promise((resolve) => requestAnimationFrame(resolve));
  }

  return module._RunPendingReadbacksFinish(contextHandle);
}
```

这里的 `await` 位于 JavaScript 浅层调度器中，而不是 `SurfaceReadback::lockPixels()` 内部。WebAssembly 栈在 `await` 之前已经返回到 JavaScript，因此无需保存 Google Test 到 readback 的深层调用栈。

#### 5.3.5 对现有同步 API 的兼容策略

`Surface::readPixels()` 可以保留为同步兼容层，但 WebGPU 测试和新代码应优先使用显式异步 readback。同步兼容层仍可继续依赖 Asyncify，因此它的目标是兼容旧调用，而不是降低 `ASYNCIFY_STACK_SIZE`。

建议分层如下：

```text
Legacy synchronous path:
Surface::readPixels()
  -> SurfaceReadback::lockPixels()
  -> may use Asyncify internally

WebGPU optimized path:
Surface::asyncReadPixels()
  -> SurfaceReadback::requestMapAsync()
  -> outer scheduler awaits readiness
  -> SurfaceReadback::lockMappedPixels()
```

这种分层可以保证迁移过程可控：未迁移代码继续工作；迁移后的 WebGPU readback 路径不再依赖深层 Asyncify 挂起。

#### 5.3.6 预期收益与验收标准

上移异步边界后，Asyncify 不再需要在 `Surface::readPixels()` 深层保存完整调用栈。若所有 WebGPU readback 测试均迁移到显式异步路径，则 `ASYNCIFY_STACK_SIZE` 可重新下调并验证，例如从 256KB 逐步回退到 128KB、64KB。

验收标准：

1. `SurfaceReadback::requestMapAsync()` 在 WebGPU 后端不执行同步等待。
2. `SurfaceReadback::lockMappedPixels()` 只允许读取已经 ready 的 buffer，不触发 Promise 等待。
3. WebGPU 测试入口能够在 JavaScript 浅层调度器中等待 pending readback。
4. 迁移后的 readback 测试在 64KB `ASYNCIFY_STACK_SIZE` 下通过。
5. 未迁移的同步 `Surface::readPixels()` 路径保持可用，并明确标记为兼容路径。

### 5.4 第四阶段：评估 JSPI

JSPI 可作为长期替代方案，但不建议在当前阶段直接替换 Asyncify。评估项包括：

1. 目标浏览器版本覆盖率。
2. Emscripten 对 JSPI 的稳定性。
3. Google Test 入口函数与 `ccall/cwrap` 调用方式兼容性。
4. C++ 虚函数、函数指针、间接调用路径下的行为。
5. 与 Web Worker 环境的兼容性。

只有当这些约束明确满足后，JSPI 才适合作为生产级替代。

---

## 6. 风险分析

### 6.1 `ASYNCIFY_IMPORTS` 配置不完整

若某个实际会挂起的 JavaScript 函数未列入 `ASYNCIFY_IMPORTS`，Asyncify 可能不会为其上游调用链插桩，导致挂起恢复失败。该类问题通常表现为运行时异常，而非编译错误。

缓解措施：

- 将所有 `EM_ASYNC_JS` 或 Promise-await 边界集中管理。
- 为每个异步导入函数建立清单。
- 增加 readback、图片解码、文件访问等异步路径测试。

### 6.2 `ASYNCIFY_REMOVE` 误删必要路径

若排除模式覆盖了实际可能进入异步调用链的函数，恢复时将缺少必要状态机代码。

缓解措施：

- 只排除确定为纯 CPU、无异步导入调用的第三方库。
- 排除规则保持窄化，避免过宽通配符。
- 每次更新第三方库后重新验证。

### 6.3 栈大小经验化

单纯依赖“某个数值当前测试能过”是不充分的。编译器内联、优化等级、测试框架变化都可能改变调用帧布局。

缓解措施：

- 建立最坏路径测试，例如深层 Google Test 调用中的连续 readback。
- Debug 与 Release 分别验证。
- 在文档中记录当前推荐值的实验依据和适用范围。

---

## 7. 结论

Asyncify 优化必须区分两个维度：

1. **插桩范围维度**：由 `ASYNCIFY_IMPORTS` 和 `ASYNCIFY_REMOVE` 控制，目标是降低 wasm 体积、编译时间和非异步路径运行开销。
2. **栈保存容量维度**：由 `ASYNCIFY_STACK_SIZE` 控制，目标是保证实际挂起时调用栈能够完整保存。

`ASYNCIFY_IMPORTS` 不能从根本上降低 `ASYNCIFY_STACK_SIZE`，因为它不改变挂起时的真实调用栈。若要系统性降低栈需求，必须调整架构，将异步边界从 `Surface::readPixels()` 深层上移到测试调度器、WebGPU readback 调度器，或最终引入显式异步 API / JSPI。

因此，推荐策略是：短期采用 `ASYNCIFY_IMPORTS + ASYNCIFY_REMOVE + 256KB stack` 稳定测试；中期量化插桩收益；长期通过上移异步边界或 JSPI 取消对大 Asyncify 栈的依赖。

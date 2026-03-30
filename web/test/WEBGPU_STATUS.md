# WebGPU 后端当前状态

**日期**: 2026-03-25
**测试结果**: 397 个测试全部运行完成，无崩溃
- ✅ 101 个通过
- ❌ 296 个失败

## 已完成的修复

### 1. SurfaceReadback `!locked` 断言崩溃
- **文件**: `src/core/SurfaceReadback.cpp`
- **问题**: `lockPixels()` 中 `map()` 返回 nullptr 时仍设 `locked=true`，析构时触发 `DEBUG_ASSERT(!locked)`
- **修复**: `map()` 返回 nullptr 时直接 return，不设 locked

### 2. LoadAction::DontCare 映射错误导致 Dawn 断言
- **文件**: `src/gpu/webgpu/WebGPUUtil.cpp`
- **问题**: `LoadAction::DontCare` 映射到 `WGPULoadOp_Undefined`（sentinel 值），Dawn 验证层断言失败
- **修复**: 映射到 `WGPULoadOp_Clear`（WebGPU 没有 DontCare loadOp）

### 3. Shader 编译失败后 Pipeline 级联崩溃
- **文件**: `src/gpu/webgpu/WebGPURenderPipeline.cpp`, `src/gpu/webgpu/WebGPURenderPass.cpp`
- **问题**: Tint 拒绝某些 shader 后，pipeline 对象虽非 null 但内部 WGPURenderPipeline 为 null，传给 `setPipeline` 导致 PAGE ERROR
- **修复**:
  - `Make()` 在 `pipeline==nullptr` 时返回 nullptr
  - `setPipeline()` 检查 `webgpuRenderPipeline()` 非 null
  - `draw()`/`drawIndexed()` 检查 `currentPipeline` 非 null

### 4. setScissorRect 负值导致 JS 验证错误
- **文件**: `src/gpu/webgpu/WebGPURenderPass.cpp`
- **问题**: 负数 int 强转 uint32_t 导致超大值
- **修复**: clamp 负值到 0，调整 width/height

### 5. WebGPU 异步 Buffer Mapping 接口
- **文件**: `src/gpu/webgpu/WebGPUBuffer.cpp`, `src/gpu/webgpu/WebGPUBuffer.h`
- **状态**: 底层接口 `requestMapAsync()` / `isReady()` 已实现，但上层未接入
- **影响**: readPixels 返回 false，是 296 个测试失败的主因

## 待修问题

### P0: WebGPU async readback（#14）
- **影响**: 296 个测试失败的根本原因
- **现状**: `WebGPUBuffer::requestMapAsync()` 和 `isReady()` 已实现，但 `SurfaceReadback` / `readPixels` 上层流程尚未对接
- **难点**: WebGPU `wgpuBufferMapAsync` 回调只在返回 JS event loop 时触发；Emscripten 单线程模型下需要 ASYNCIFY 或重构为异步调用模型
- **ASYNCIFY 尝试结果**: WASM 从 227MB 膨胀到 292MB，测试 stall

### P1: TextureUsage 缺少 RenderAttachment（#15）
- **影响**: 4 个 WebGPU 验证错误，全部在 `VectorLayerTest.StrokeDashAdaptive`
- **错误纹理**: 1080x1776、1280x720、200x200（2次），均为 RGBA8Unorm
- **现象**: 纹理 WGPUTextureUsage 为 7（CopySrc|CopyDst|TextureBinding），缺少 RenderAttachment（0x10）
- **分析**: log 中 `usage=7` 表示输入 tgfx TextureUsage 只有 `TEXTURE_BINDING`（没有 `RENDER_ATTACHMENT`），但这些纹理后来被用作 render pass 的 color attachment
- **待查**: 定位创建这些纹理的上层代码路径，确认是否应在创建时加上 `RENDER_ATTACHMENT`

### P1: Invalid RenderPipeline（#16）
- **影响**: 24 个 Invalid RenderPipeline → 81 个 Invalid CommandBuffer
- **根因**: Tint SPIR-V reader 拒绝 `textureSample` 在 non-uniform control flow 中的使用（WGSL 规范要求）
- **已有保护**: pipeline/draw 路径的 null check
- **长期方案**:
  1. 修改 GLSL shader，在 non-uniform flow 中用 `textureSampleLevel` 替代 `textureSample`
  2. 或在 SPIR-V→WGSL 转换中做 workaround

### P2: Scissor rect 超出 render target 范围（#18）
- **现象**: `Scissor rect (x: 0, y: 0, width: 316, height: 316) is not contained in the render target dimensions (300 x 300)` 等
- **修复方案**: 在 `setScissorRect` 中 clamp 到 render target 维度

### P3: 清理调试日志（#17）
- **范围**: WebGPURenderPass、WebGPURenderPipeline、WebGPUTexture、WebGPUCommandEncoder 中的 `emscripten_console_logf` 调用
- **时机**: 所有 runtime 问题修完后

## 关键文件

| 文件 | 说明 |
|------|------|
| `src/gpu/webgpu/WebGPUUtil.cpp` | 枚举映射（LoadOp/StoreOp/TextureUsage/BlendFactor 等） |
| `src/gpu/webgpu/WebGPURenderPass.cpp` | Render pass 编码，scissor/draw/bind group |
| `src/gpu/webgpu/WebGPURenderPipeline.cpp` | Pipeline 创建，shader module null check |
| `src/gpu/webgpu/WebGPUShaderModule.cpp` | GLSL→SPIR-V→WGSL 转换（Tint） |
| `src/gpu/webgpu/WebGPUBuffer.cpp` | Buffer 创建，staging 写入，async readback |
| `src/gpu/webgpu/WebGPUTexture.cpp` | 纹理创建，usage flag 转换 |
| `src/gpu/webgpu/WebGPUCommandEncoder.cpp` | Command encoder，texture copy |
| `src/core/SurfaceReadback.cpp` | 像素回读（map/unmap） |
| `web/test/run_test.js` | Puppeteer 测试运行器 |
| `web/test/index.html` | 测试入口页面 |

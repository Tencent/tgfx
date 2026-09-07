# TGFX 文档目录

## 当前有效方案（设计文档）

| 文档 | 说明 |
|------|------|
| [`android-vulkan-integration.md`](./android-vulkan-integration.md) | Android Vulkan 接入技术方案 |
| [`shader-permutation-system-design.md`](./shader-permutation-system-design.md) | Shader Permutation 方案设计规范（v2.1，当前方案） |
| [`aot-architecture-analysis.md`](./aot-architecture-analysis.md) | AOT L1/L2/L3 第一性原理分析（基于代码与实测数据，含优先级） |
| [`aot-materialization-safety.md`](./aot-materialization-safety.md) | AOT 物化安全边界（L2 多 pass 的准入判据，含两次实测回归的根因） |
| [`multi-backend-test-decision.md`](./multi-backend-test-decision.md) | 多后端截图测试方案评审 |
| [`webgpu-technical-architecture.md`](./webgpu-technical-architecture.md) | TGFX WebGPU 后端技术体系 |

## `guides/` 知识科普

面向团队成员的技术学习资料，非项目专属设计文档。

- [`vulkan-quick-start.md`](./guides/vulkan-quick-start.md) — Vulkan 入门
- [`vulkan-advanced.md`](./guides/vulkan-advanced.md) — Vulkan 进阶
- [`metal-quick-start.md`](./guides/metal-quick-start.md) — Metal 入门
- [`webgpu-overview.md`](./guides/webgpu-overview.md) — WebGPU 概述
- [`dawn-and-wgpu-analysis.md`](./guides/dawn-and-wgpu-analysis.md) — Dawn/wgpu 实现对比分析
- [`font-rendering.md`](./guides/font-rendering.md) — 字体渲染原理

## `research/` 调研与竞品分析

- [`flutter-impeller-shader-architecture.md`](./research/flutter-impeller-shader-architecture.md) — Flutter Impeller Shader 架构调研
- [`baseline-testing.md`](./research/baseline-testing.md) — 截图测试容差机制行业调研
- [`shader-precompile-master-plan.md`](./research/shader-precompile-master-plan.md) — Shader 多 Pass 分解方案（已被 `shader-permutation-system-design.md` 取代，作调研参考保留）

## `reports/` 时间点调查报告

特定时间点产出的调查/排障日志，参考价值随问题修复而递减。

- [`gl-vulkan-divergence-report-2026-05-18.md`](./reports/gl-vulkan-divergence-report-2026-05-18.md) — SwiftShader GL/VK 差异报告
- [`mac-gl-vs-vk-conclusion.md`](./reports/mac-gl-vs-vk-conclusion.md) — Mac GL vs VK 截图差异分析
- [`vk-six-real-bugs-root-cause-2026-05-19.md`](./reports/vk-six-real-bugs-root-cause-2026-05-19.md) — 6 类真 Bug 根因分析

## `presentation/`

Shader Permutation 技术评审/汇报 PPT 及生成脚本。

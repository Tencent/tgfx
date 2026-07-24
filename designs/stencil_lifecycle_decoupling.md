## 1 背景

### 1.1 问题定义

当前 stencil 由各 `RenderTargetProxy` 私有创建：首次需要时直接 `gpu()->createTexture()` 生成，存入自己的 `stencilTexture` 强引用，获取路径不经过任何缓存查找。

- `src/gpu/proxies/RenderTargetProxy.h:168`（`stencilTexture` 私有强引用）
- `src/gpu/proxies/RenderTargetProxy.cpp:65-78`（`getOrAllocateStencil` 直接 createTexture，无缓存查找）
- `src/gpu/tasks/OpsRenderTask.cpp:41`（execute 阶段按需取 stencil，来源仍是 proxy 私有字段）

因此同规格的多个 proxy 各自持有独立的 stencil，无法互相复用。

对照 Skia：`GrRenderTarget` 同样把 attachment 持有到自身生命周期结束（`skia/src/gpu/ganesh/GrRenderTarget.cpp:34-39`），但获取时经 `GrResourceProvider::attachStencilAttachment` 按 spec 派生 unique key 命中共享（`skia/src/gpu/ganesh/GrResourceProvider.cpp:689-708`），故同规格的多个 RT 共享同一块物理 stencil，显存占用取决于不同 spec 数而非 RT 数。

因此，stencil 无法复用的根因不在于 proxy 持有 stencil（Skia 的 RT 同样持有 attachment），而在于 TGFX 的获取路径未接入缓存管理：资源既不被登记，也无从被复用。

### 1.2 目标

让同规格的多个 RenderTarget 复用同一块 stencil，降低 stencil 显存占用。

### 1.3 术语定义

- **帧内复用**：同一次 flush/encode 内，按提交顺序的多个 render pass 复用同一 stencil 资源。
- **跨帧复用**：当前帧释放后的 stencil 在后续帧再次命中复用。

## 2 现有实现分析

### 2.1 TGFX 当前缓存架构全景

| 层级/模块 | 主要职责 | 关键数据结构/约束 |
|---|---|---|
| `ResourceCache` | 管理可缓存 GPU 资源生命周期（TextureView、RenderTarget、Buffer 等） | `nonpurgeableResources` / `purgeableResources`、`scratchKeyMap`、`uniqueKeyMap`；默认 512MB、120 帧过期、scratch 2 帧快过期（`src/gpu/ResourceCache.h:138-150`，`src/gpu/ResourceCache.cpp:27`，`:86-101`） |
| `GlobalCache` | 管理 Context 生命周期内的“结构性资源”（Program、Gradient、索引缓冲、Uniform 缓冲池、static resources） | Program LRU 上限 256（`src/gpu/GlobalCache.cpp:33`，`:194-199`）；Gradient LRU 上限 32（`:34`，`:228-233`） |
| Atlas System（`AtlasStrikeCache` + `AtlasManager` / `Atlas`） | 文字渲染缓存：管理字形元数据（AtlasGlyph）与 CPU 内存池，以及 GPU 图集纹理页与 Plot 淘汰 | 字形元数据 4MB / 2048 上限、超限按 LRU 释放到约 1/4（`src/core/AtlasStrikeCache.h:79-80`，`src/core/AtlasStrikeCache.cpp:76-91`）；图集页 `MaxResidentPages = 4`、页满按 token 驱逐旧 plot（`src/core/AtlasTypes.h:115`，`src/core/Atlas.cpp:55-66`） |
| `ReturnQueue` | 跨线程归还 `ReturnNode`，由缓存线程统一消费 | `ConcurrentQueue` + 自定义 deleter 回调（`src/core/utils/ReturnQueue.h:57`，`src/core/utils/ReturnQueue.cpp:29-49`） |
| `BlockAllocator` | CPU 侧块内存 arena：按块分配大量小对象、`clear()` 复用块，降低分配开销（非 GPU 资源缓存） | 由 `Context::drawingAllocator()` 暴露供 drawing 期临时对象使用（`src/core/utils/BlockAllocator.h:52`） |

补充边界：`Context::memoryUsage()` 只统计 `ResourceCache` 字节，不含 `GlobalCache` / `Atlas` / proxy 私有 stencil（`src/gpu/Context.cpp:165-168`）。

### 2.2 TGFX ResourceCache 的键模型与回收语义

**键模型**：

- Scratch（规格复用）：Texture/RT/Buffer 都按规格生成 scratch key（`src/gpu/resources/TextureView.cpp:48-59`，`src/gpu/resources/TextureRenderTarget.cpp:26-38`，`src/gpu/resources/BufferResource.cpp:22-30`）。
- Unique（身份复用）：Resource 通过 `assignUniqueKey()` 进入 unique 路径（`src/gpu/resources/Resource.h:74`，`src/gpu/resources/Resource.cpp:22-35`）。

**回收与降级**：

`purgeableResources`（可回收链）的清理由 `purgeResourcesByLRU` 统一执行，有三个触发时机：(1) 每帧末的 `recordFrameAndPurge -> purgeAsNeeded`（`src/gpu/ResourceCache.cpp:78-101`）；(2) 外部主动压显存的 `purgeUntilMemoryTo`（`:68-72`）；(3) 外部按时间点清理的 `purgeNotUsedSince`（`:62-66`）。清理并非“清空整条链”：链按 LRU 排序（最久未用在前），遍历从最老的开始，一旦遇到满足保留条件的资源就 `break`，其后全部保留（`:103-110`）。每帧末的保留条件是“显存未超上限（默认 512MB）”，超限时再叠加“最近 `_expirationFrames`（默认 120 帧）内使用过”（`:88-95`）——即近 120 帧用过、或显存未超限的资源都会被保住，这正是跨帧复用成立的基础。

1. scratch 命中有 `completedFrameTime` 门槛，天然更偏跨帧复用（`src/gpu/ResourceCache.cpp:167-174`）。
2. 资源在最后一个强引用释放后经 `processUnreferencedResources` 进入上述 LRU 链；若自身带 ScratchKey 或有外部 UniqueKey 则保留待复用，否则直接释放（`src/gpu/ResourceCache.cpp:131-143`）。
3. 降级并非进入 purgeable 链时发生，而是在上述 LRU 清理扫到该资源、且当轮未因保留条件提前 `break` 时（即长期未用且显存超限的兜底场景）才触发：此时若资源已 purgeable（无 `shared_ptr` 强引用）、仍有外部 UniqueKey 持有、且带 ScratchKey，则解除 unique 身份、降级为纯 scratch 资源继续按规格复用，而非直接释放（`src/gpu/ResourceCache.cpp:115-123`）。
4. 注意“外部 UniqueKey 持有”与“`shared_ptr` 强引用”是两个维度：前者由 `uniqueKey.useCount() > 1` 判定（资源自身持 1 份，外部再持 1 份即 >1），后者由 `isPurgeable()`（`weakThis.expired()`）判定资源是否可回收（`src/gpu/resources/Resource.h:93-99`，`src/gpu/resources/ResourceKey.cpp:150-152`）。

### 2.3 Stencil 资源在当前 TGFX 中属于哪一类

当前 stencil 不属于上面任一“统一缓存层”，而是 **RenderTargetProxy 私有 attachment 缓存**：

1. `RenderTargetProxy` 内部保存 `stencilTexture` 强引用（`src/gpu/proxies/RenderTargetProxy.h:168`）。
2. 首次需求时走 `getOrAllocateStencil()` 直接 `gpu()->createTexture()` 创建 DEPTH24_STENCIL8（`src/gpu/proxies/RenderTargetProxy.cpp:65-78`）。
3. `OpsRenderTask` 仅在有 `needsStencil()` 的 op 时挂载它（`src/gpu/tasks/OpsRenderTask.cpp:39-54`）。
4. 对近似分档 RT，stencil 尺寸取 backingStore 尺寸（`src/gpu/proxies/TextureRenderTargetProxy.h:71-73`）。

因此 stencil 的现状类别可定义为：

- **类别**：render pass depth/stencil attachment（非通用纹理内容缓存）
- **归属层**：`RenderTargetProxy` 私有生命周期
- **复用范围**：同一 proxy 生命周期内复用；跨 proxy 不复用；不经过 Scratch/Unique 索引

### 2.4 现架构与本次改造关系

| 观察 | 直接影响 |
|---|---|
| stencil 不在 ResourceCache/GlobalCache | 需让 stencil 的获取经 `ResourceCache` 按 unique-key 命中共享，而非仅调 `ResourceCache` 参数 |
| 现有 scratch 命中依赖 completedFrameTime | scratch 路径受门槛限制无法帧内复用（§2.5），需走 `ResourceCache` 内 unique-key 命中共享（存活期即可共享），而非 scratch 池 |
| `TextureRenderTargetProxy` 获取 stencil 时已按 backingStore 尺寸申请 | 本方案 key 沿用各 proxy 实际申请的尺寸口径精确匹配 |
| 当前 stencil 由 proxy 私有持有、随 proxy 存活 | 当前每个 proxy 各自私有 stencil、无跨 proxy 共享；改造后 proxy 仍持有 stencil，但经 unique-key 命中改为持有共享块（多 proxy 共享同一块） |

由此可得分析结论：stencil 要支持复用，必须接入 `ResourceCache`、按 spec 派生 unique-key 共享。

### 2.5 scratch 与 unique 复用语义对 stencil 的含义

TGFX 的 `ResourceCache` 同时管理 scratch 与 unique 两类可复用资源，两者的复用触发条件差异决定了它们对 stencil 的适用性。

**scratch（规格复用）**：借出**不从 `scratchKeyMap` 移除**（`src/gpu/ResourceCache.cpp:275-283` 才摘除），同规格多份以 vector 并存。命中须同时满足（`findScratchResource`，`src/gpu/ResourceCache.cpp:162-184`）：

1. `isPurgeable()`：`weakThis.expired()`，当前无任何 `shared_ptr` 持有者（`src/gpu/resources/Resource.h:93-95`）；
2. `!hasExternalReferences()`：无外部 UniqueKey 持有（`:97-99`）；
3. `lastUsedTime <= completedFrameTime`：GPU 已完成该资源上次使用所在的帧。

因此 scratch 是**独占 + 仅跨帧**复用：同规格 scratch key 天然可跨 RT，但借出即独占、活跃期间不可被共享，且**同帧内不可复用**（本帧释放的资源 `lastUsedTime = currentFrameTime`，恒 > `completedFrameTime`）。

**unique（身份复用）**：`getUniqueResource`（`src/gpu/ResourceCache.cpp:198-213`）只要求 `hasExternalReferences()`（`uniqueKey.useCount() > 1`），**不要求 isPurgeable**。因此 unique 资源在**存活期间即可被并发共享**：多个 RenderTarget 只要解析到同一把 UniqueKey，就拿到同一个资源，天然支持**帧内 + 跨帧、跨 RT 共享**。

| 维度 | scratch | unique |
|---|---|---|
| 命中前提 | isPurgeable + 无外部引用 + `lastUsedTime <= completedFrameTime` | 仅需 `hasExternalReferences()` |
| 存活期间可否被共享 | ❌ | ✅ |
| 同帧内可否复用 | ❌（completedFrameTime 门槛） | ✅ |
| 跨帧跨 RT 复用 | ✅ | ✅ |

**对 stencil 的结论**：文档目标（同规格 RenderTarget 共享一块 stencil 以降低显存占用，§4.3）需要“存活期间可共享”——同帧内多个同规格 stencil 使用点能命中同一块，而非空闲后才复用，只有 unique 路径满足，scratch 受 completedFrameTime 门槛无法达成。故 stencil 复用应建立在 unique-key spec 共享之上（与 Skia §3.5 一致），而非 scratch 池。

**一个 TGFX 特有约束**：UniqueKey 是**身份制**（`UniqueDomain` 指针，`src/gpu/resources/ResourceKey.cpp:119-121`），两把独立 `Make()` 创建、spec 相同的 key 并不相等。但 `UniqueKey::Append(domain, data)` 会沿用传入 key 的 domain 编码 spec 数据（`:107-110`），同 domain + 同 spec 必得同 key。故按 spec 共享只需一张**静态共享 domain** 以 `Append` 派生稳定 key（对齐 Skia 的 static kDomain），无需登记表。

## 3 参考设计

### 3.1 Skia Ganesh 当前缓存架构全景

| 层级/模块 | 主要职责 | 关键数据结构/约束 | 与 stencil 的关系 |
|---|---|---|---|
| `GrResourceCache` | 统一管理 GPU 资源生命周期、预算与回收 | `fPurgeableQueue`、`fNonpurgeableResources`、`fScratchMap`、`fUniqueHash`（`skia/src/gpu/ganesh/GrResourceCache.h:372-378`）；默认预算上限 256MB（`:89`） | stencil 共享最终落到这里 |
| `GrResourceProvider` | 统一创建/查找入口（先查 cache，miss 再创建） | scratch 查询 `findAndRefScratchResource`（`skia/src/gpu/ganesh/GrResourceProvider.cpp:330-337`）；unique 查询 `findByUniqueKey`（`:432-435`） | stencil attach 路径入口 |
| `GrResourceAllocator` | flush 期区间规划（gather/plan/assign），做 register 级别复用 | `Interval` / `Register` / `FreePool`（`skia/src/gpu/ganesh/GrResourceAllocator.h:212`，`:175`，`:153-162`） | 不直接管理 stencil |

**主链路（flush）**：

Skia 在渲染期对通过 Proxy 管理的懒加载 GPU 资源（普通 color/texture）采用统一的规划-分配策略，flush 时按以下顺序执行。注意 RenderTarget 的附件资源不属于此类通用资源，stencil 即附件的一种，其管理方式见 §3.5。

1. gather intervals：`task->gatherProxyIntervals(...)`（`skia/src/gpu/ganesh/GrDrawingManager.cpp:201`）
2. plan：`resourceAllocator.planAssignment()`（`:203`）
3. assign：`resourceAllocator.assign()`（`:205`）
4. execute：`executeRenderTasks(...)`（`:209`）
5. flush 后 cache purge：`resourceCache->purgeAsNeeded()`（`:217`，`:225`）

### 3.2 ResourceCache 的复用判定与 purgeable 迁移机制

本节回答“资源何时可复用、复用是否只能取可淘汰队列里的资源”，这是理解“为什么 stencil 选 unique-key 式共享而非 scratch 池”的缓存层依据。

**两个容器**：

- `fPurgeableQueue`：按 timestamp 排序的 LRU 待回收队列（不是复用池）。
- `fNonpurgeableResources`：仍被引用的资源数组。
- 复用查找实际走 `fScratchMap`（scratch）或 `fUniqueHash`（unique），不是遍历 purgeable 队列。
- 位置：`skia/src/gpu/ganesh/GrResourceCache.h:372`、`:373`、`:376`、`:378`

**进入 purgeable 的唯一入口**：资源最后一个 ref 归零时触发 `notifyARefCntReachedZero`：

- 仍有 ref 或 command buffer usage 时直接返回，不进队列：`skia/src/gpu/ganesh/GrResourceCache.cpp:328`
- 完全无引用后从 nonpurgeable 移除并入 purgeable 队列：`skia/src/gpu/ganesh/GrResourceCache.cpp:349-351`

**进队后“保留待复用”还是“立即释放”**：

- 预算内且带 key（scratchKey 或 uniqueKey）→ 保留在队列等待复用：`skia/src/gpu/ganesh/GrResourceCache.cpp:361-363`
- 无 key 或超预算 → 立即 `release()`：`skia/src/gpu/ganesh/GrResourceCache.cpp:383`
- 普通 scratch 纹理靠 scratchKey 命中保留分支；stencil 靠 uniqueKey 命中保留分支（stencil 无 scratchKey，见 §3.5）。

**scratch 与 unique 的本质区别：identity vs interchangeability**

两者的根本差异不是“能否跨 RT 共享”，而是**身份（identity）vs 可互换（interchangeability）**；机制根源是两个查询函数的行为差异：

- `findAndRefScratchResource` 命中后**立即 `fScratchMap.remove(...)` 把资源摘出**再 ref（`skia/src/gpu/ganesh/GrResourceCache.cpp:213-222`）→ 借出即独占，同一份不可能被两方同时持有。
- `findAndRefUniqueResource` 命中**只 ref、不摘出**，资源留在 `fUniqueHash`（`skia/src/gpu/ganesh/GrResourceCache.h:160-166`）→ 键↔实例稳定映射，引用期间可被多方反复命中、同时共享。

“能否同时共享”只是推论：scratch 命中即摘出 → 天然独占；unique 命中不摘出 → 多方可持同一份。scratch 也非“不能跨 RT”，而是“命中即独占，不存在同一份被同时持有”。stencil 选 unique 正是为“稳定身份 + 引用期间反复命中同一实例”以支持同帧并发共享（§3.5）；注意 unique key 仍按规格算，此处 identity 指“键↔实例的稳定映射”，不是“绑定某个 RT”。

**复用判定差异（含 purgeable / 在途维度）**：

| 维度 | scratch 纹理 | stencil（unique key） |
|---|---|---|
| 键代表 | 一类规格，任取一个空闲同规格 | 一个稳定身份，精确命中那一个 |
| 命中后是否摘出 | 摘出 → 独占 | 不摘出 → 共享 |
| 登记容器 | `fScratchMap`（索引层） | `fUniqueHash` |
| 进入可复用状态前提 | 主引用 `fRefCnt` 归零即可（不要求命令缓存引用归零） | key 有效即可 |
| 复用时是否必须在 purgeable 队列 | 否 | 否 |
| 帧内复用（持有者未释放时被再次命中） | ✅（Skia 侧主引用归零即可，见下） | ✅ |

- scratch 复用入口 `findAndRefScratchResource` 命中后**先 `fScratchMap.remove(...)` 把资源摘出池**，再 `refAndMakeResourceMRU`（仅当资源在 purgeable 队列时拉回 nonpurgeable，否则只加引用）：
  - `skia/src/gpu/ganesh/GrResourceCache.cpp:213-222`、`:295`
  - 摘出是 identity/interchangeability 区别的机制根源（见本节开头）。
- scratch 候选前提 `isUsableAsScratch = isScratch() && !internalHasRef()`——只要求**主引用**（`fRefCnt`）归零，**不要求命令缓存引用**（`fCommandBufferUsageCnt`）归零：
  - `skia/src/gpu/ganesh/GrGpuResourceCacheAccess.h:41`
- 因此存在一个易被误读的中间态：主引用归零、命令缓存引用仍 >0 的资源，会在主引用归零时被写入 `fScratchMap`，但因 `hasRefOrCommandBufferUsage()` 为真而**提前返回、留在 `fNonpurgeableResources`**——即同时位于 NonPurgeable 数组与 ScratchMap，可在 GPU 仍在途时被 scratch 复用：
  - 主引用归零即写入 ScratchMap 并早返回：`skia/src/gpu/ganesh/GrResourceCache.cpp:321`、`:327`
  - `fScratchMap` 成员恒等价于 `isUsableAsScratch()`（validate 不变式）：`skia/src/gpu/ganesh/GrResourceCache.cpp:797`、`:826`
- unique 查找 `findByUniqueKey` 同样不要求资源无引用，因此可在持有者未释放时共享命中——这是 stencil 能帧内复用的根本原因。
- 关键区分：**purgeable ≠ usable-as-scratch**。资源必须始终位于 NonPurgeable 或 PurgeableQueue 之一（`skia/src/gpu/ganesh/GrResourceCache.cpp:875`），而 ScratchMap 只是叠加在其上的复用索引，不是第三个并列容器（`skia/src/gpu/ganesh/GrResourceCache.h:376`）。scratch 只能跨帧复用的实际约束不在 Skia cache 状态机，而在 TGFX 侧的 `completedFrameTime` 门槛（TGFX 对比见 §3.3）。

**scratch 资格的单一守门点（源头在构造期）**：资源能否被当 scratch 命中，其资格的根是成员 `fScratchKey` 是否有效；该值在构造期 `registerWithCache` 内由虚函数写入 `this->computeScratchKey(&fScratchKey)`（`skia/src/gpu/ganesh/GrGpuResource.cpp:36`，成员声明 `GrGpuResource.h:332`）。`GrAttachment::computeScratchKey()` 对 stencil/texture 不写 key（`GrAttachment.cpp:101-113`），故 stencil 的 `fScratchKey.isValid()` 恒 false → `isScratch()`/`isUsableAsScratch()`（`GrGpuResourceCacheAccess.h:36`、`:41`）恒 false → 所有入池点统一跳过 → stencil **永不进入 `fScratchMap`**。因此，尽管存在静态的 `ComputeScratchKey`（`GrAttachment.cpp:85`）可对 stencil usage 算出 key，但 stencil 从未进入 `fScratchMap`，拿该 key 去查也只会 miss→新建，不会误命中其他资源。

**先入队再判断的设计取舍**：即使资源注定被销毁，代码也先 `fPurgeableQueue.insert` 再在 `release()/removeResource` 中移除，看似多一步，实为维护“资源逻辑状态必须与所在容器一致”的不变式，让所有销毁走统一路径、记账成对加减：

- 无条件入队与记账：`skia/src/gpu/ganesh/GrResourceCache.cpp:350-352`
- `removeResource` 统一按 purgeable 状态移除并回冲记账：`skia/src/gpu/ganesh/GrResourceCache.cpp:114-116`

### 3.3 主引用 vs 命令缓存引用

§3.2 中“scratch 可用不要求命令缓存引用归零”这一结论的底层，是 Ganesh 对资源使用状态的**双计数**模型。理清它，才能弄清各后端如何在 GPU 未执行完时安全地释放或复用资源。

本节聚焦**生命周期层**——保证资源在 GPU 用完之前不被 CPU 端释放或复用覆写，与 §3.4 的**执行层**（保证 GPU 命令按依赖顺序访问仍有效的资源）正交：执行层调度访问顺序、不管资源会不会被 CPU 端销毁；本层管生命周期、不管命令先后。二者防的是不同事故。

**两个计数的定义**：

- 主引用 `fRefCnt`：Skia 侧逻辑使用（普通 ref/unref）。
- 命令缓存引用 `fCommandBufferUsageCnt`：GPU 在途使用计数（命令缓冲已引用该资源、尚未执行完）。作用是追踪资源是否仍被未完成的 GPU 命令引用，据此避免在 GPU 用完之前把资源释放或用作它途，即使 Skia 侧已无逻辑引用。

两个计数及 `hasRef` / `hasNoCommandBufferUsages` 定义于 `skia/src/gpu/ganesh/GrGpuResource.h`。

**两条判定的差异（关键）**：

- purgeable = 主引用归零 **且** 命令缓存引用归零 **且** 非（kUnbudgetedCacheable 且带 uniqueKey）：`skia/src/gpu/ganesh/GrGpuResource.cpp:104`。
- usable-as-scratch = `isScratch() && !internalHasRef()`，只看主引用：`skia/src/gpu/ganesh/GrGpuResourceCacheAccess.h:41`。

因此“主引用归零但 GPU 仍在途”的资源可被 scratch 复用而不必等 GPU 完成——`gr_cb` 的注释明确说这就是设计目的（“allows for a scratch resource to be reused for new draw calls even if it is in use on the GPU”）：`skia/src/gpu/GpuRefCnt.h:170`。

**计数如何加减**：命令缓存引用由 `gr_cb<const GrSurface>` 容器驱动——资源放入即 `refCommandBuffer`（+1），析构 / clear 即 `unrefCommandBuffer`（-1）；命令缓冲用 `STArray<..., gr_cb<const GrSurface>>` 跟踪在途 surface，command buffer 执行完或命令池 reset 时统一 clear（`skia/src/gpu/GpuRefCnt.h:178`）。

**如何得知“依赖命令执行完”并放掉计数（完整闭环）**：不是用阻塞式同步函数拦截，而是靠 GPU 完成信号的**非阻塞轮询** + **析构联动**：

1. 提交：每个已提交的 command buffer 记入 outstanding 队列（按提交顺序），其在途 surface 此时都在各自的 `gr_cb` 容器里、计数 >0（Metal `GrMtlGpu.mm:228`）。
2. 轮询：在合适时机（如提交新 buffer、flush）调 `checkForFinishedCommandBuffers`，从队首查每个 buffer 是否已完成——Metal 查 `MTLCommandBuffer` 完成 status（`isCompleted`，`GrMtlGpu.mm:246-259`），Vulkan 查 `VkFence`（`vkGetFenceStatus`）。队列有序，遇到第一个未完成的即停。此为非阻塞查询，不卡 CPU。
3. 放计数：已完成的 command buffer 被 pop 并析构，析构中 `fTrackedGrSurfaces.clear()`（`GrMtlCommandBuffer.mm:61`）清空 `gr_cb` 数组，每个元素析构触发 `unrefCommandBuffer`，`fCommandBufferUsageCnt` 归零。此后该资源才对 `ResourceCache` 变为可回收。

仅当显式要求“等 GPU 全部完成”时才走阻塞路径（`finishOutstandingGpuWork` 的 `waitUntilCompleted` / Vulkan `vkWaitForFences`，`GrMtlGpu.mm:267`），对应 `submit(syncCpu=true)` 等场景，非日常回收路径。

**后端差异（谁维护在途计数）**：区别在于**底层 API 是否要求应用自己保证资源在途生命周期**。

- **Vulkan / Metal**（显式用 `gr_cb` 追踪）：底层 API 不保证“资源在 GPU 用完前其底层内存不被释放”，而 Skia 的 `ResourceCache` 一旦发现某资源逻辑引用归零，就可能立即释放或重新分配它的 GPU 内存——若此时 command buffer 尚未执行完，在途命令就会访问到已失效 / 被挪用的内存。故两者都把在途 surface 用 `gr_cb<const GrSurface>` 挂在 command buffer 上，`fCommandBufferUsageCnt` 保持 >0，使其在途期间不被判为可回收，直到该 buffer 执行完才统一 clear——Vulkan 为 `fTrackedGpuSurfaces`（`skia/src/gpu/ganesh/vk/GrVkCommandBuffer.h:181`），Metal 为 `fTrackedGrSurfaces`（`skia/src/gpu/ganesh/mtl/GrMtlCommandBuffer.h:110`、`addGrSurface`、`GrMtlCommandBuffer.mm:61`）。（注意：这只解决“CPU 端在 GPU 用完前回收内存”的生命周期问题；GPU 指令间访问同一资源的先后顺序由执行层负责，见 §3.4。）
- **OpenGL / D3D**（不用 `gr_cb`）：GL 侧未见 `gr_cb`——`glDeleteTextures` 等的真正释放由驱动延迟到 GPU 用完，生命周期安全由驱动兜底；复用覆写场景则由 **orphaning** 保证：整体重指资源内容（如 `glBufferData`/`glTexImage`，区别于部分更新的 `glBufferSubData`/`glTexSubImage`）且旧命令仍在途时，驱动为新内容另开一块存储、让对象名指向它，旧命令继续读旧存储，二者互不干扰，旧存储待旧命令完成后回收。D3D 有 command list，资源生命周期靠上层 `sk_sp` 持有到提交完成、`ExecuteCommandLists` 后按 fence 回收。

因此“命令缓存引用”（`fCommandBufferUsageCnt` / `gr_cb`）在 Ganesh 中由 **Vulkan 与 Metal** 使用，OpenGL 靠驱动兜底、D3D 靠 `sk_sp` + fence。

关键区别不在“资源销毁指令是否立即执行”，而在**谁负责保证“GPU 用完前底层内存不被释放/重分配”**：

| 后端 | 底层是否自己保证在途安全 | 由谁负责 | Skia 是否需显式追踪 |
|---|---|---|---|
| Vulkan / Metal | 否（销毁 API 不检查 GPU 在途，早释放即 UB） | Skia 应用层：`gr_cb` / `fCommandBufferUsageCnt` | ✅ 需要 |
| OpenGL | 是（驱动延迟真正释放到 GPU 用完，或 orphaning） | GL 驱动 | ❌ 不需要 |
| D3D | 部分（command list 提交后按 fence 回收） | 上层 `sk_sp` 持有到提交完成 | ❌ 不走 `gr_cb` |

### 3.4 Skia 的 GrResourceAllocator

**适用条件**：普通 surface/texture 的 flush 内资源规划与复用。

**阶段边界（gather -> plan -> assign -> execute）**：

`GrResourceAllocator` 只覆盖前三阶段，execute 时资源已就绪。四阶段依次为 gather（`skia/src/gpu/ganesh/GrDrawingManager.cpp:201`）、plan（`:203`）、assign（`:205`）、execute（`:209`）。

**核心数据结构**：`Interval`（资源活跃区间 `[start, end]`，`skia/src/gpu/ganesh/GrResourceAllocator.h:212`）、`Register`（逻辑寄存器，承载可复用 allocation，`:175`）、`FreePool`（按 `ScratchKey` 回收/再分配，类型别名 `SkTMultiMap<Register, ScratchKey>`，`:161`）。

**关键流程**：

调用入口为 `GrDrawingManager.cpp:181`；gather 由各 RenderTask 的 `gatherProxyIntervals` 登记（`skia/src/gpu/ganesh/ops/OpsTask.cpp:913`）。

1. gather 阶段：`gatherProxyIntervals` 把每个 proxy 的读/写使用映射到 op 时间轴，登记为闭区间 `[start, end]` 的生命周期数据（不是简单收集 proxy 列表）。target 登记写区间、sampled 点状登记、deferred 用 `ActualUse::kNo` 占位；每步 `incOps()` 推进时间轴（`OpsTask.cpp:938` target、`:956` sampled、`:926` deferred、`:969` incOps）。
2. addInterval 阶段：同一 proxy 多次出现会扩展 `end`（区间合并）；仅 `ActualUse::kYes` 计入 use；`AllowRecycling::kNo` 直接阻断后续可回收路径（`GrResourceAllocator.cpp:55`，区间合并 `:105`、use 计数 `:100`）。
3. plan 阶段：先 `expire(curStart)` 弹出 active 中已结束 interval，可回收则把其 register 放入 freePool；再把当前 interval 插入 active 并尝试复用/分配 register（`GrResourceAllocator.cpp:344` planAssignment）。
4. assign 阶段：遍历 finished intervals，把 register 解析为实际 surface（已实例化跳过、lazy 在此实例化）（`GrResourceAllocator.cpp:431`）。

**复用与回收策略**：

- `expire(curIndex)` 回收已结束区间的 register 到 freePool：
  - `skia/src/gpu/ganesh/GrResourceAllocator.cpp:326`、`:338`
- `findOrCreateRegisterFor()` 优先从 free pool 命中：
  - `skia/src/gpu/ganesh/GrResourceAllocator.cpp:295`
- 回收条件受 `isRecyclable()` 约束（AllowRecycling、scratch key 有效、无 unique key、无外部引用；含 Vk secondary CB 特例）：
  - `skia/src/gpu/ganesh/GrResourceAllocator.cpp:145`
- 复用的是底层 register/surface，不是 proxy 对象本身（proxy 仍各自独立）：
  - `skia/src/gpu/ganesh/GrResourceAllocator.cpp:295`、`:200`

**具体复用示例（同规格 A/B）**：设同帧内 P1 写 proxy A、P2 读 A，随后 P3 需要临时 proxy B，且 A/B 规格一致。A 与 B 是两个独立 interval（不会合并），但 A 的区间在 P2 后结束、`expire()` 把其 register 按 ScratchKey 放回 free pool，B 到来时从 free pool 命中同一 register，最终落到同一底层 surface。这说明 allocator 的复用粒度是 **“不同 interval 共享同一 register”**，而非 “同一 interval”：
- expire 回收：`skia/src/gpu/ganesh/GrResourceAllocator.cpp:326`
- free pool 命中：`skia/src/gpu/ganesh/GrResourceAllocator.cpp:311`、`:317`
- assign 落到 proxy：`skia/src/gpu/ganesh/GrResourceAllocator.cpp:200`

**关键约束（避免误读）**：

- “区间不重叠”是必要非充分条件，还须通过 `isRecyclable` 全部判定才复用。
- `planAssignment` 只做规划与回收窗口推进，不创建 GPU 资源；`assign` 才实例化绑定。
- `copy/resolve/wait/transfer/writePixels` 等无普通绘制 op 的 task 会用 fake op 占位以保持时间轴一致：
  - `skia/src/gpu/ganesh/GrCopyRenderTask.cpp:60`、`skia/src/gpu/ganesh/GrTextureResolveRenderTask.cpp:80`、`skia/src/gpu/ganesh/GrWaitRenderTask.cpp:15`、`skia/src/gpu/ganesh/GrTransferFromRenderTask.cpp:14`、`skia/src/gpu/ganesh/GrWritePixelsRenderTask.cpp:53`

**复用为何不与 GPU 流水线并行冲突（规划层 + 执行层两道保证）**：

GPU 命令是乱序 / 流水线并行执行的，仅靠“提交顺序上区间不重叠”不足以保证正确——后一个 op 可能在前一个 op 对复用资源的访问尚未完成时就并行开跑。allocator 的复用之所以安全，是因为它只是**规划层**的一半，另一半由**执行层的资源屏障**兜底：

- 规划层（`GrResourceAllocator`）：用 liveness 保证只有活跃区间不重叠的 proxy 才共享同一 register（前者 `expire` 后者才可能命中），即“谁能复用谁”。
- 执行层（后端保证访问顺序）：当一个 register 从前一个 op 转给后一个 op 访问时，后端确保前者的访问完成后后者才开始，抵消流水线并行。按“谁来保证”分两类：Vulkan 由 Skia **应用层显式**插 barrier；Metal 与 OpenGL 则由**底层（驱动 / 运行时）自动**插入同步，应用层无需介入。各后端机制如下：
  - Vulkan：`VkImageMemoryBarrier` / layout 转换。`GrVkImage::setImageLayoutAndQueueIndex` 按“旧 layout/访问 -> 新 layout/访问”算出 src/dst 的 access mask 与 pipeline stage 并构造 barrier（`skia/src/gpu/ganesh/vk/GrVkImage.cpp:472`），最终经 `GrVkCommandBuffer::pipelineBarrier` 落到 `CmdPipelineBarrier`（`skia/src/gpu/ganesh/vk/GrVkCommandBuffer.cpp:84`、`:180`），保证 srcStage 的工作完成后 dstStage 才开始。
  - Metal：默认自动 hazard tracking 隐式同步（与 OpenGL 同属“底层自动同步”，由 Metal 运行时而非应用层插入）。一个 command buffer 生命周期内含一串顺序 encoder（同一时刻至多一个活跃，由 `fActiveRenderCommandEncoder`/`fActiveBlitCommandEncoder` 持有）；每次取新 encoder 前先 `endAllEncoding` 结束当前活跃的那个（`GrMtlCommandBuffer.mm:208`）。encoder 只保证按编码顺序**启动**，GPU 仍可让无依赖的命令跨 encoder 流水线重叠；当后一个 encoder 访问前一个写过的资源时，由 Metal 的自动 hazard tracking（`[queue commandBuffer]` 默认 `MTLHazardTrackingModeTracked`，`GrMtlCommandBuffer.mm:32`）检测依赖并隐式插入同步。Skia 应用层不显式设 fence/barrier，也未开启 texture barrier（`GrMtlCaps.mm:421` 中 `fTextureBarrierSupport = false`），依赖 Metal 运行时的这层隐式同步。
  - OpenGL：由驱动隐式保证（与 Metal 同属“底层自动同步”），无需应用层介入——要么隐式同步（等 GPU 用完旧内容），要么在整体重指内容时做 orphaning（为新内容另开存储，旧命令继续读旧存储），详见 §3.3。

两层缺一不可：规划层给出“可复用”的静态结论，执行层的访问顺序保证兑现“访问按序”的动态保证。因此该复用对所有后端都正确，不依赖某一后端的特殊行为。这也是它与 §3.3 讨论的 scratch 在途复用的根本差别——后者无法静态证明不重叠，只能靠帧级门槛或命令缓存引用兜底。

### 3.5 Skia 中 stencil 资源归类与缓存关系

承接 §3.4：RenderTarget 的附件资源（stencil、MSAA color 等）不经上节 `GrResourceAllocator` 通用规划路径，而是在 render pass 执行期由 `GrResourceProvider` 按需附着、经缓存命中复用。本节聚焦其中的 stencil。

Skia 中 stencil 不是 RT 私有强持有，而是 **provider 入口 + unique key 共享 attachment**：

1. `OpsTask::onExecute` 检测 `needsStencil()` 后调用 `resourceProvider()->attachStencilAttachment(...)`（`skia/src/gpu/ganesh/ops/OpsTask.cpp:578-581`）。
2. `attachStencilAttachment` 先按 format/dimensions/sample/usage 计算 shared unique key，再 `findByUniqueKey` 命中复用，miss 则创建并 `assignUniqueKeyToResource`（`skia/src/gpu/ganesh/GrResourceProvider.cpp:689-698`，`:701-708`）。
3. `GrRenderTarget` 仅持 attachment 引用，release/abandon 时清空引用（`skia/src/gpu/ganesh/GrRenderTarget.cpp:34-39`，`:41-46`）。

同时，Skia 明确把 stencil 排除出 attachment 的 scratch 缓存路径：

- `GrAttachment::computeScratchKey()`（实例方法）对 `UsageFlags::kStencilAttachment` / `kTexture` 直接不写 scratch key（`skia/src/gpu/ganesh/GrAttachment.cpp:101-113`）。

**为什么 stencil 用 unique 而非 scratch**：stencil 逻辑上“无特定指向、同规格即可复用”，直觉像 scratch；但选 unique 是为支持**同帧并发共享**（引用期间反复命中同一实例），而非仅“空闲后复用”。`GrAttachment.h:59-60` 注释明说该 unique key 用于“shared between multiple render targets at the same time”。这与 scratch “命中即摘出、独占”的语义互斥（机制细节见 §3.2）。

**unique 不等于绑定具体对象**：`ComputeSharedAttachmentUniqueKey`（`skia/src/gpu/ganesh/GrAttachment.cpp:69`）与 `ComputeScratchKey`（`:85`）共用同一 `build_key`（尺寸/格式/usage/sample/protected/memoryless），unique key 仍按规格算。差别只在键的**类型**（稳定身份索引 vs 规格池索引），不在键的输入。

**用哪种键不由外部任选**：`GrAttachment` 是 stencil / MSAA color /（Vulkan）texture-image 的共同基类，基类同时提供两个静态键工具，但同一对象生命周期只走一条，由 usage 经实例 `computeScratchKey()` 在构造期分流——stencil / texture 不生成 scratch key，纯 color attachment 才生成。MSAA color 确有两条路，但由上层 API 决定契约：`getDiscardableMSAAAttachment` 走 unique-key（可共享、内容随时被覆盖、结束丢弃，`skia/src/gpu/ganesh/GrResourceProvider.cpp:739`），`makeMSAAAttachment` 走 scratch-key（独占，`:812`）。

补充：oversized stencil 分支在 Ganesh 中存在但当前禁用（`#if 0`，`skia/src/gpu/ganesh/GrResourceProvider.cpp:677-682`），实际仍按 `rt->dimensions()` 精确匹配（`:691`）。

**小结**：Skia 的 stencil 归类为“shared attachment + unique identity reuse”，经 provider 与主缓存体系衔接，而非 RT 私有持有。

**复用时的物理同步**：相邻 render pass 复用同一块 stencil 时，语义上无数据依赖——stencil 每 pass 起始清零、结束丢弃（Skia 侧 `GrLoadOp::kClear` / `GrStoreOp::kDiscard`），内容不跨 pass 携带；但物理内存层面仍存在 WAR/WAW（前一个 pass 的 stencil 写命令可能仍在 GPU 流水线、后一个 pass 的 Clear/写已开始操作同一块内存）。该物理顺序由后端同步保证（见 §3.4 执行层）：Vulkan 在 begin render pass 时对 stencil attachment 转 layout 到 `DEPTH_STENCIL_ATTACHMENT_OPTIMAL`、以 `EARLY_FRAGMENT_TESTS` 为 dstStage 发 barrier（`skia/src/gpu/ganesh/vk/GrVkOpsRenderPass.cpp:139-153`）；Metal / OpenGL 同属底层自动同步（hazard tracking / 驱动隐式），应用层无需介入。

**与 `GrResourceAllocator` 的关系（正交）**：
需要明确边界：上节 `GrResourceAllocator` 只管理普通 color/texture 类 surface proxy，**不覆盖 stencil**；stencil 始终走本节的 `GrResourceProvider` 独立路径。两者互不参与。证据：

1. `addInterval` 入参类型仅为 `GrSurfaceProxy*`，登记的都是普通纹理资源：
   - `skia/src/gpu/ganesh/GrResourceAllocator.cpp:55`
   - 各 RenderTask 的 `gatherProxyIntervals` 只登记 target/sampled/deferred/copy 等普通 proxy，无一处登记 stencil：
     - `skia/src/gpu/ganesh/ops/OpsTask.cpp:938`（target）、`:956`（sampled）、`:926`（deferred）
     - `skia/src/gpu/ganesh/GrCopyRenderTask.cpp:68`
2. 时序上 stencil 附着晚于 allocator 全流程。allocator 在规划阶段跑完 `gatherProxyIntervals -> planAssignment -> assign`，随后才 `executeRenderTasks`：
   - `skia/src/gpu/ganesh/GrDrawingManager.cpp:201`（gather）
   - `skia/src/gpu/ganesh/GrDrawingManager.cpp:205`（assign）
   - `skia/src/gpu/ganesh/GrDrawingManager.cpp:209`（execute）
   - stencil 在 execute 阶段的 `OpsTask::onExecute` 内按需附着：`skia/src/gpu/ganesh/ops/OpsTask.cpp:580`
   - `addInterval` 有 `SkASSERT(!fAssigned)`，assign 之后不允许再登记 interval，stencil 天然无法进入：`skia/src/gpu/ganesh/GrResourceAllocator.cpp:59`
3. 复用键机制不同且互斥。allocator 的 free pool 按 `ScratchKey` 复用，而 stencil 被显式排除出 scratchKey 体系、只用 UniqueKey 共享：
   - free pool 按 ScratchKey：`skia/src/gpu/ganesh/GrResourceAllocator.cpp:154`、`:317`
   - stencil 排除 scratchKey：`skia/src/gpu/ganesh/GrAttachment.cpp:101`

### 3.6 oversized stencil 在 Skia 的现状

Skia 代码中存在 oversized 分支，但当前禁用（`#if 0`）：

- `skia/src/gpu/ganesh/GrResourceProvider.cpp:677`

实际仍按 `rt->dimensions()` 精确匹配：

- `skia/src/gpu/ganesh/GrResourceProvider.cpp:689`
- `skia/src/gpu/ganesh/GrResourceProvider.cpp:701`

结论：**“禁用”是事实；“禁用原因”在现有注释与代码中无直接证据，属于未知原因/未知风险。**

## 4 对比分析

### 4.1 策略对比

| 策略 | 生命周期管理 | 帧内复用 | 跨帧复用 | 适配成本 | 与现有架构匹配 |
|---|---|---|---|---|---|
| TGFX 现状（proxy 私有 stencil） | 绑定 proxy | ❌ | ⚠️（同 proxy） | 低 | ✅ |
| 本方案（spec 派生 UniqueKey + ResourceCache 共享） | ResourceCache 统一管理（记账/LRU/降级） | ✅ | ✅ | 低-中 | ✅ |
| GrResourceAllocator 全量化 | 全资源活跃区间统一规划 | ✅ | ✅ | 高 | ⚠️（需架构改造） |

最直觉的 scratch 池路径因 `completedFrameTime` 门槛无法帧内复用，与租借归还池一并作为被否决方案，论证见 §5.2。

### 4.2 Skia 与 TGFX 的机制对比

本节汇总 §3 各机制引出的方案选型依据与 Skia / TGFX 横向对照。

**机制映射总览**（Skia 做法与本方案的对应，本方案细节见 §5.4–5.6）：

| 维度 | Skia | 本方案 |
|---|---|---|
| 获取入口 | `GrResourceProvider::attachStencilAttachment`（`GrResourceProvider.cpp:689`） | `RenderTargetProxy::getStencil` 内联 attach（`ComputeSharedAttachmentUniqueKey` + `Resource::Find`） |
| 共享键来源 | `ComputeSharedAttachmentUniqueKey` 按 spec 算（static kDomain） | spec 派生 UniqueKey：共享静态 domain + `Append` 编码 spec（对齐 Skia static kDomain） |
| 可缓存资源 | `GrAttachment`（stencil / MSAA color 共同基类） | `DepthStencilTextureView : DefaultTextureView`（stencil 专用） |
| 命中 / 创建 | `findByUniqueKey` 命中 / `assignUniqueKeyToResource` 创建 | `Resource::Find` 命中 / `MakeFrom` + `assignUniqueKey` 创建 |
| 持有方与持有期 | `GrRenderTarget` 持 attachment 至自身生命周期结束 | `RenderTargetProxy` 持 `DepthStencilTextureView` 至自身生命周期结束（与 Skia 一致） |
| 复用语义 | unique-key 存活期同帧并发共享 | 同（unique-key 存活期共享）；空闲跨帧再经 unique→scratch 降级按规格复用 |

两边机制同构：RT/proxy 持有共享 attachment 至自身销毁，共享静态 domain 编码 spec 派生 unique key。差异仅在实现形式——Skia 经 `GrResourceProvider::attachStencilAttachment` 入口、附件为 `GrAttachment` 基类；本方案由 `RenderTargetProxy::getStencil` 内联完成 attach（无 provider 层）、附件为 `DepthStencilTextureView : DefaultTextureView`（不取 `DepthStencilAttachment` 一名是因为 `include/tgfx/gpu/RenderPass.h:123` 已存在同名的 render pass 描述结构体）。

**选型依据：stencil 走 unique-key 而非 scratch 池**。要达到 Skia 那样的帧内复用，stencil 必须走“key 命中即共享、不要求无引用”的 unique-key 式机制，不能套用受 `completedFrameTime` 门槛限制的 scratch 池（机制见 §3.5、§3.2），与 §2.5 的结论一致。TGFX 侧对应约束点为 `src/gpu/ResourceCache.cpp:162-174`（`findScratchResource` 要求 `lastUsedTime <= completedFrameTime`）。

**在途保护机制对比（Skia vs TGFX）**：

- TGFX 的 `Resource` **没有** per-resource 命令缓存引用计数，只有帧级 `lastUsedTime` 与 `uniqueKey.useCount()`（`src/gpu/resources/Resource.h:91`、`:97-98`）。GPU 在途保护靠 `findScratchResource` 的 `lastUsedTime <= completedFrameTime` 帧级门槛（`src/gpu/ResourceCache.cpp:162-174`），这也是 §2.5 中“scratch 池只能跨帧”的真正来源。
- TGFX 当前是单队列，一次 `flush` 内所有 render pass 记录在同一个 encoder 上顺序提交（`src/gpu/DrawingBuffer.cpp:46`），资源回收发生在帧末；GPU 在途期间资源不被回收，由上述帧级门槛保证，而非 per-resource 在途计数。
- 相邻 render pass 复用同一块附件（含 stencil）物理内存时的访问顺序，由后端在 pass 之间同步保证——Vulkan 在 begin render pass 时对 depth/stencil attachment 做 `TransitionImageLayout` 转 layout 到 `DEPTH_STENCIL_ATTACHMENT_OPTIMAL`（`src/gpu/vulkan/VulkanRenderPass.cpp:174-176`，内部即 `vkCmdPipelineBarrier`），Metal / OpenGL 由 hazard tracking / 驱动隐式处理（§3.4）。

**GrResourceAllocator 路径对 stencil 的正交性**：即便 TGFX 后续引入 `GrResourceAllocator` 式的普通纹理 liveness 规划，也不会顺带解决 stencil 复用（正交性论证见 §3.5）；stencil 的独立 provider 路径（本文档主方案）仍是必需的、正交的一层。

### 4.3 性能与内存量化

**理论上限（标注：理论）**

设 stencil 单块大小：

- `S = width * height * sampleCount * 4 bytes`（DEPTH24_STENCIL8 视作 4 bytes/px）

设同规格（相同 backingStore 尺寸 / sampleCount）且需要 stencil 的存活 RenderTarget 有 `N` 个：

- 现状常驻占用上限：`N * S`（每个 proxy 各自持有一块，直到自身销毁才释放）
- 本方案常驻占用上限：`1 * S`（同规格共享一块）

示例（1920x1080, sampleCount=1）：

- `S ≈ 1920*1080*4 = 7.9 MB`
- 若 `N=4`：现状上限约 `31.6 MB`，方案上限约 `7.9 MB`

**实际可观察值（标注：实际）**

实际占用会低于理论上限，受两类因素影响：

1. 存活 RenderTarget 并非都需要 stencil；
2. 多个 RenderTarget 可能 spec 不同（尺寸/sampleCount 不同）无法共享，各自独立占用。

文档中的容量估算用于比较趋势，不替代实测。

## 5 重构实现方案

### 5.1 设计目标

- 主方案：将 stencil 包装为可缓存 `Resource`，纳入现有 `ResourceCache`，按 **backingStore spec 派生的 UniqueKey** 在所有同规格 RenderTarget 间共享同一块 depth/stencil attachment（对齐 Skia shared-unique-key 模型，§3.5）。
- 复用 `ResourceCache` 既有能力：显存记账、LRU 过期、unique→scratch 降级回收（`src/gpu/ResourceCache.cpp:116-123`）。不新增与 `ResourceCache`/`ProxyProvider` 并列的 Provider / Pool / 租借归还池层。
- 仅在 stencil 资源范围改造，不扩展到普通中间 color 纹理别名。

### 5.2 选型

本节保留两个被否决方案，用于论证为何不采用更“直接”的做法。其中“租借归还池”指仿照连接池/对象池的显式借还模型：用完的 stencil 通过显式“归还”回到一个专用池，而非依赖 `shared_ptr` 析构自然回缓存。

| 方案 | 同帧复用 | 跨帧复用 | 需新增机制 | 否决/采纳理由 |
|---|---|---|---|---|
| scratch 池 | ❌ | ✅ | 仅加 scratchKey | `findScratchResource` 的 completedFrameTime 门槛堵死同帧复用（§2.5），无法达成 §4.2 主目标 |
| 租借归还池 | ✅ | ✅ | StencilPool + StencilLease + Provider | 需重造 `ResourceCache` 已有的记账/LRU/状态机；且 Skia 并非此模型，属过度设计 |
| **unique-key spec 共享** | ✅ | ✅ | 仅加静态 spec key 派生函数（共享 domain） | `getUniqueResource` 存活期即可共享（§2.5），语义与 Skia 一致，改动最小 —— **采纳** |

### 5.3 正确性依据

`OpsRenderTask::execute` 为 stencil attachment 设置 `loadAction = Clear`（stencilClearValue = 0）与 `storeAction = DontCare`（`src/gpu/tasks/OpsRenderTask.cpp:52-58`）。即 stencil **每个 pass 起始清零、结束丢弃，内容从不跨 pass 携带**。因此“所有同规格 RenderTarget 共享同一块物理 stencil”不改变任何 pass 的语义输出，在语义上安全——这正是 unique-key 共享成立的前提，与 Skia 依赖的不变式一致。物理内存层面的访问顺序由后端 pass 间同步保证，见 §5.7。

### 5.4 数据结构与 spec 键派生

```cpp
struct DepthStencilSpec {
  int width = 0;
  int height = 0;
  int sampleCount = 1;
  PixelFormat format = PixelFormat::DEPTH24_STENCIL8;
};

// depth/stencil 附件资源：复用 DefaultTextureView 包裹纹理并进入 ResourceCache。
// memoryUsage 直接按 width*height*4*sampleCount 计算（DEPTH24_STENCIL8 视作 4 bytes/px）——
// 基类按 PixelFormatBytesPerPixel 记账，而该函数对 depth/stencil 格式返回 0，无法沿用。
class DepthStencilTextureView : public DefaultTextureView {
 public:
  size_t memoryUsage() const override;

  // 创建 depth/stencil 纹理并包装进缓存：内部 createTexture（OOM 返回 nullptr）、
  // AddToCache 挂 spec 派生 scratch key、assignUniqueKey 挂 unique key。
  static std::shared_ptr<DepthStencilTextureView> MakeFrom(Context* context,
                                                           const DepthStencilSpec& spec,
                                                           const UniqueKey& uniqueKey);

  // 按 spec 派生稳定的 UniqueKey：共享 domain + Append 编码 spec，同 spec 必得同 key。
  // 命名对齐 Skia 同名函数，预留扩展到 MSAA color 等其他共享附件类型。
  static UniqueKey ComputeSharedAttachmentUniqueKey(const DepthStencilSpec& spec);

  // 按 spec 派生 scratch key（含 width/height/sampleCount/format，不含 domain），
  // 用于空闲 unique->scratch 降级后按规格跨帧复用。独立类型标识避免与其他资源冲突。
  static ScratchKey ComputeDepthStencilScratchKey(const DepthStencilSpec& spec);

  int sampleCount() const;

 private:
  int _sampleCount = 1;

  DepthStencilTextureView(std::shared_ptr<Texture> texture, int sampleCount);
};
```

UniqueKey 是身份制（§2.5），独立 `Make()` 的 key 互不相等；共享一个 domain 后，`Append` 派生的 key 只随 spec 变化。仿照 Skia 的 static kDomain，`ComputeSharedAttachmentUniqueKey` 内部用一个静态共享 domain 派生 spec key：

```cpp
UniqueKey DepthStencilTextureView::ComputeSharedAttachmentUniqueKey(const DepthStencilSpec& spec) {
  static const UniqueKey domain = UniqueKey::Make();  // 共享 domain（对齐 Skia static kDomain）
  uint32_t data[] = {static_cast<uint32_t>(spec.width), static_cast<uint32_t>(spec.height),
                     static_cast<uint32_t>(spec.sampleCount), static_cast<uint32_t>(spec.format)};
  return UniqueKey::Append(domain, data, 4);
}
```

静态 `domain` 恒持一份引用，资源 `uniqueKey` 再持一份，`useCount >= 2` 使 `findUniqueResource` 的 `hasExternalReferences()` 恒真，资源在缓存期间始终可命中共享；空闲后的 unique→scratch 降级由 `ResourceCache` 既有逻辑统一处理，无需额外登记表。

### 5.5 核心流程伪代码

```cpp
// RenderTargetProxy::getStencil —— 首次 attach 时经 unique key 共享/创建并缓存到 proxy，
// 之后直接返回持有字段，不再重复 Find。
std::shared_ptr<DepthStencilTextureView> RenderTargetProxy::getStencil(int sampleCount) {
  if (stencilAttachment != nullptr) {
    DEBUG_ASSERT(stencilAttachment->sampleCount() == sampleCount);
    return stencilAttachment;
  }
  DepthStencilSpec spec = {stencilWidth(), stencilHeight(), sampleCount,
                           PixelFormat::DEPTH24_STENCIL8};
  auto key = DepthStencilTextureView::ComputeSharedAttachmentUniqueKey(spec);
  stencilAttachment = Resource::Find<DepthStencilTextureView>(getContext(), key);
  if (stencilAttachment == nullptr) {
    // unique 查找 miss 时再按 spec 的 scratch key 查找：命中的是被缓存压力降级为 scratch 的
    // 同规格空闲 attachment，重新 assignUniqueKey 提升回共享身份；未命中才新建（OOM 返回
    // nullptr）、挂 scratch/unique 双 key 进缓存。
    stencilAttachment = Resource::Find<DepthStencilTextureView>(
        getContext(), DepthStencilTextureView::ComputeDepthStencilScratchKey(spec));
    if (stencilAttachment != nullptr) {
      stencilAttachment->assignUniqueKey(key);
    } else {
      stencilAttachment = DepthStencilTextureView::MakeFrom(getContext(), spec, key);
    }
  }
  return stencilAttachment;
}

OpsRenderTask::execute(encoder) {
  descriptor = RenderPassDescriptor(...)
  if (hasStencilOp) {
    stencil = renderTargetProxy->getStencil(renderTarget->sampleCount())
    if (stencil != nullptr) {                    // OOM 时为 nullptr，不挂载，needsStencil 的 op 随之跳过
      descriptor.depthStencilAttachment.texture = stencil->getTexture()
    }
  }

  pass = encoder->beginRenderPass(descriptor)
  drawOps(pass)
  pass->end()
}
```

`MakeFrom` 内以 `ComputeDepthStencilScratchKey` 派生的 scratch key 调 `Resource::AddToCache`、以 `ComputeSharedAttachmentUniqueKey` 派生的 unique key 调 `assignUniqueKey`，使资源同时具备双键；空闲后的 unique→scratch 降级与跨帧复用见 §5.4。

### 5.6 集成改造点

| 文件 | 改造内容 |
|---|---|
| `src/gpu/proxies/RenderTargetProxy.h/.cpp` | `stencilTexture` 字段类型由 `Texture` 改为 `DepthStencilTextureView`（`stencilAttachment`）；`getStencil()` 由直接 createTexture 改为 `ComputeSharedAttachmentUniqueKey` 派生 key、`Resource::Find` 命中共享、unique miss 后按 scratch key 命中降级资源并 `assignUniqueKey` re-promote、最终 miss 才 `MakeFrom` 创建；新增 `stencilWidth()/stencilHeight()` 描述接口 |
| `src/gpu/proxies/TextureRenderTargetProxy.h` | `stencilWidth()/stencilHeight()` 返回 `_backingStoreWidth/_backingStoreHeight` |
| `src/gpu/resources/DepthStencilTextureView.h/.cpp`（新增） | depth/stencil 附件资源：继承 `DefaultTextureView` 包裹纹理，override `memoryUsage` 直接按 `width*height*4*sampleCount` 记账；含静态 `ComputeSharedAttachmentUniqueKey` / `ComputeDepthStencilScratchKey` 派生 spec 双键 |
| `src/gpu/tasks/OpsRenderTask.cpp` | 改为经 `renderTargetProxy->getStencil()` 取 proxy 持有的共享 attachment，以 `stencil->getTexture()` 填入 render pass descriptor |
| `src/gpu/opengl/qt/QGLDrawableProxy.h/.cpp` | `getStencil()` 返回类型同步为 `std::shared_ptr<DepthStencilTextureView>`，转发逻辑不变 |

### 5.7 正确性边界

1. key 必须完全一致（backingStore width/height/sampleCount/format），任一不同即分配独立 stencil。
2. 共享依赖 §5.3 的 per-pass 清零/丢弃不变式；若未来引入依赖 stencil 内容跨 pass 保留的绘制，须为该场景单独分配非共享 stencil。
3. 失败路径保持现有行为：`createTexture` 失败（OOM）时跳过 `needsStencil()` 的 op（`src/gpu/tasks/OpsRenderTask.cpp:42-51`）。
4. 记账一致性（参考 §3.2）：`DepthStencilTextureView::memoryUsage()` 必须如实上报底层纹理显存并纳入 `ResourceCache` 统一记账，避免“记为 0 导致永不回收”或重复计账。
5. proxy 持有与共享：stencil 由 proxy 持有至自身销毁（与 Skia RT 持有 attachment 一致），但多个同 spec proxy 经 unique key 共享同一块 `DepthStencilTextureView`，物理占用为每 spec 一块而非每 proxy 一块；proxy 销毁后 stencil 无强引用、经 `ResourceCache` 按 spec 复用或按 LRU 回收。
6. 多 pass 共享同一 stencil 的安全性分两层，均无需新增 per-resource 在途计数：
   - 生命周期层：stencil 由 proxy 持有（共享块），proxy 存活期间不会被释放，GPU 在途安全由 proxy 持有的强引用与帧级门槛保证（依据见 §4.2）。
   - 执行层：相邻 pass 复用同一块 stencil 物理内存的 WAR/WAW 顺序，由 TGFX 各后端现有的 render pass attachment 同步机制覆盖，方案无需新增适配——Vulkan 在 begin render pass 时对 depth/stencil attachment 做 `TransitionImageLayout` barrier（`src/gpu/vulkan/VulkanRenderPass.cpp:174-176`）；Metal 自动 hazard tracking、OpenGL 驱动隐式、WebGPU 运行时自动，后三者均无需手动 barrier（`src/gpu/webgpu/WebGPUCaps.cpp:41` `textureBarrier = false`）。又因 stencil 每 pass 清零、内容不跨 pass 携带（§5.3），pass 间无数据依赖，只需内存访问顺序的常规同步——即各后端已保证相邻 render pass 并发执行时访问同一 stencil 的数据安全，方案无需额外处理。

### 5.8 后续优化方向

Skia `GrResourceAllocator` 提供了“普通 surface 的统一 liveness 规划 + 别名复用”思路，可作为下一阶段方向：

1. 将 `DrawingManager` 的 render task 线性编号，记录中间纹理区间；
2. 按 `expire + freePool` 做别名复用；
3. 先落地离线规划（plan），再落地真实分配（assign）。

该方向解决的是“为什么不直接让所有中间纹理都帧内复用”：

- 仅有 GPU 按提交顺序执行不够，需要严格 liveness/hazard 证明；
- 当前链路存在 copy/mipmap/readback 等后续依赖（如 `src/gpu/DrawingManager.cpp:108`、`src/gpu/DrawingManager.cpp:118`），必须先建立统一活跃区间模型。

需要注意：该路径与本方案的 stencil 解耦是**正交的两层**（详见 §3.5）。GrResourceAllocator 只管理普通 color/texture 资源，不覆盖 stencil；因此即使未来落地该路径，stencil 仍需保留本文档主方案的独立共享路径。

## 6 测试计划

### 6.1 单元测试

目标文件：`test/src/GPURenderTest.cpp`（除特别标注外）

1. `RenderTargetProxyGetStencil`（替换旧用例，旧语义依赖 proxy 私有缓存）：首次 `getStencil()` 惰性 attach，后续调用返回 proxy 持有字段且 `ResourceCache` 字节数不再增长（即 proxy 持有字段复用、不重复创建）。
2. `RenderTargetProxySharesStencilAcrossProxies`：同 spec 两个 proxy 的 `getStencil()` 解析到同一 `DepthStencilTextureView` 实例（验证共享静态 domain 派生的 spec key）。
3. `RenderTargetProxyStencilSpecIsolation`：不同尺寸的 proxy 各自分配到不同实例。
4. `RenderTargetProxyStencilDowngradeReuse`：proxy 释放后 `purgeUntilMemoryTo(0)` 触发超预算清理，空闲 attachment 被降级为 scratch 资源而非销毁（断言 unique 查找 miss、purgeable 字节数非零）；新建同 spec proxy 经 scratch 命中 re-promote 回复用实例（指针相等）。
5. `Dispatch_StencilReuseAcrossPasses`（目标文件 `test/src/StencilCoverPathTest.cpp`）：两个同 spec `Surface` 交替绘制，每轮迭代向两个 Surface 各画一个不同蒙版的五边形并 flush，25 轮共 50 个 render pass 复用同一块 stencil（每次 pass 起始清零、pass 内重写蒙版），经 `Baseline::Compare`（key：`StencilCoverPath/StencilReuseAcrossPassesA` / `...B`）验证无跨 pass 残留污染——端到端验证 §5.3 的 per-pass 瞬态与 §5.7 的执行层数据安全。

### 6.2 边界情况

1. `sampleCount` 不一致。
2. backingStore 尺寸不一致（含 Approx 分档边界）。
3. stencil 创建失败（OOM）回退路径。

### 6.3 后端覆盖

覆盖 OpenGL / Vulkan / Metal / WebGPU，验证：

- depthStencil attachment 绑定合法；
- stencil pass 输出正确；
- 复用不会污染后续 pass。

## 7 实现步骤

| 阶段 | 目标 | 涉及文件 | 产出 |
|---|---|---|---|
| Phase 1 | 引入 `DepthStencilTextureView` 可缓存资源与 spec 双键派生函数（`ComputeSharedAttachmentUniqueKey` / `ComputeDepthStencilScratchKey`） | `src/gpu/resources/DepthStencilTextureView.*` | stencil 可进入统一缓存，按 spec 派生 unique key（共享命中）与 scratch key（降级跨帧复用） |
| Phase 2 | 改造 proxy 与 task 调用链 | `RenderTargetProxy.*`, `TextureRenderTargetProxy.h`, `OpsRenderTask.cpp`, `QGLDrawableProxy.*` | proxy 持有的 stencil 从私有创建改为经 unique-key 共享（`getStencil()` 内联 attach 并缓存到字段），`OpsRenderTask` 经 `getStencil()` 取用 |
| Phase 3 | 完成单测与后端回归 | `test/src/GPURenderTest.cpp`、`test/src/StencilCoverPathTest.cpp` + 四后端验证 | 机制单测与多 pass 复用截图用例通过 |
| Phase 4 | 文档收尾与后续路线固化 | 本文档 + 关联设计记录 | 明确下一阶段 GrResourceAllocator 借鉴计划 |

/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "DrawingManager.h"
#include "ProxyProvider.h"
#include "core/AtlasManager.h"
#include "gpu/AOTPlanExecutor.h"
#include "gpu/AOTRouter.h"
#include "gpu/PrecompiledShaderCache.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "gpu/proxies/TextureProxy.h"
#include "gpu/tasks/GenerateMipmapsTask.h"
#include "gpu/tasks/RenderTargetCopyTask.h"
#include "gpu/tasks/RuntimeDrawTask.h"
#include "tasks/TransferPixelsTask.h"

namespace tgfx {
DrawingManager::DrawingManager(Context* context) : context(context) {
}

DrawingBuffer* DrawingManager::createDrawingBuffer() {
  for (auto it = bufferPool.begin(); it != bufferPool.end(); ++it) {
    if (it->use_count() == 1) {
      currentBuffer = *it;
      bufferPool.erase(it);
      currentBuffer->reset();
      return currentBuffer.get();
    }
  }
  currentBuffer = std::make_shared<DrawingBuffer>(context);
  return currentBuffer.get();
}

PlacementPtr<DrawOp> DrawingManager::makeFillDrawOp(std::shared_ptr<RenderTargetProxy> renderTarget,
                                                    PlacementPtr<FragmentProcessor> processor,
                                                    uint32_t renderFlags, const Point& coordOffset,
                                                    OffscreenFillKey diagnosticKey) {
  if (renderTarget == nullptr || processor == nullptr) {
    return nullptr;
  }
  auto allocator = drawingAllocator();
  auto width = static_cast<float>(renderTarget->width());
  auto height = static_cast<float>(renderTarget->height());
  PlacementPtr<RectsVertexProvider> provider = nullptr;
  if (coordOffset.x == 0.0f && coordOffset.y == 0.0f) {
    auto bounds = Rect::MakeWH(width, height);
    provider = RectsVertexProvider::MakeFrom(allocator, bounds, AAType::None, true);
  } else {
    auto rect = Rect::MakeXYWH(coordOffset.x, coordOffset.y, width, height);
    auto viewMatrix = Matrix::MakeTrans(-coordOffset.x, -coordOffset.y);
    auto record = allocator->make<RectRecord>(rect, viewMatrix);
    auto rects = std::vector<PlacementPtr<RectRecord>>();
    rects.push_back(std::move(record));
    provider = RectsVertexProvider::MakeFrom(allocator, std::move(rects), {}, {}, AAType::None,
                                             false, UVSubsetMode::None, {}, nullptr, true);
  }
  if (provider == nullptr) {
    return nullptr;
  }
  PlacementPtr<DrawOp> drawOp =
      RectDrawOp::Make(renderTarget->getContext(), std::move(provider), renderFlags);
  if (drawOp == nullptr) {
    return nullptr;
  }
  drawOp->addColorFP(std::move(processor));
  drawOp->setBlendMode(BlendMode::Src);
  if (diagnosticKey != InvalidOffscreenFillKey) {
    drawOp->setOffscreenFillDiagnostic(diagnosticKey);
  }
  return drawOp;
}

bool DrawingManager::fillRTWithFP(std::shared_ptr<RenderTargetProxy> renderTarget,
                                  PlacementPtr<FragmentProcessor> processor, uint32_t renderFlags,
                                  const Point& coordOffset, OffscreenFillSource source,
                                  ColorChainRebuild rebuildColorChain) {
  auto cache = context->precompiledShaderCache();
  // The decomposition route only pays off for chains the plain route cannot match directly: a
  // composite processor tree (Compose/Xfermode children). A bare single-source fill is already
  // served by the direct matchers, so it must skip the route and keep its single program lookup.
  bool routeCandidate = cache != nullptr && cache->isLoaded() && cache->decompositionEnabled() &&
                        processor != nullptr && processor->numChildProcessors() > 0;
  bool analyze = processor != nullptr &&
                 (routeCandidate || (cache != nullptr && cache->diagnosticRecordingEnabled()));
  AOTEffectGraph graph = {};
  AOTEffectPlan plan = {};
  std::string blocker;
  bool lowerSucceeded = false;
  bool decomposeSucceeded = false;
  if (analyze) {
    lowerSucceeded = AOTEffectDecomposer::Lower({processor.get()}, &graph, &blocker);
    // Decompose only returns plans the executor runs, so this is also the production verdict.
    decomposeSucceeded = lowerSucceeded && AOTEffectDecomposer::Decompose(graph, &plan);
  }
  OffscreenFillKey diagnosticKey = InvalidOffscreenFillKey;
  if (cache != nullptr && cache->diagnosticRecordingEnabled() && processor != nullptr) {
    std::vector<AOTKernelKind> kernels = {};
    kernels.reserve(plan.passes.size());
    for (const auto& pass : plan.passes) {
      kernels.push_back(pass.kernel);
    }
    diagnosticKey =
        cache->recordOffscreenFillAnalysis(source, coordOffset != Point::Zero(), processor->name(),
                                           lowerSucceeded, blocker, decomposeSucceeded, kernels);
  }
  // Submits the plan task for the fill's draw op. The plan executor takes ownership of the op on
  // success, so the fill must stop there: falling through to the plain branch would submit a null
  // op. An offscreen fill owns a fresh target, so the task clears its full backing first and a
  // shader-side sample cannot observe pixels left by an earlier scratch allocation.
  auto submitPlanTask = [&](const AOTEffectGraph& taskGraph, const AOTEffectPlan& taskPlan,
                            PlacementPtr<DrawOp>* originalDraw) {
    // The fill covers the target's logical bounds, and its processors see coordinates shifted by
    // coordOffset; the intermediates of a multi-pass plan reproduce both.
    auto deviceBounds = Rect::MakeWH(static_cast<float>(renderTarget->width()),
                                     static_cast<float>(renderTarget->height()));
    auto task = AOTPlanExecutor::Make(context, renderFlags, taskGraph, taskPlan, deviceBounds,
                                      renderTarget, originalDraw, coordOffset, true);
    if (task == nullptr) {
      return false;
    }
    addRenderTask(std::move(task));
    addGenerateMipmapsTask(renderTarget->asTextureProxy());
    return true;
  };
  // The retry verdict reads the processor tree, so it must be taken before the processor moves
  // into the fill's draw op below.
  bool retryWarranted = routeCandidate && rebuildColorChain != nullptr &&
                        AOTRouter::RetryWarranted(processor.get(), renderFlags);
  auto drawOp =
      makeFillDrawOp(renderTarget, std::move(processor), renderFlags, coordOffset, diagnosticKey);
  if (drawOp == nullptr) {
    return false;
  }
  AOTRouter::SubmitPlan submitFill = [&](const AOTEffectGraph& taskGraph,
                                         const AOTEffectPlan& taskPlan) {
    return submitPlanTask(taskGraph, taskPlan, &drawOp);
  };
  // The original tree was already planned above (the diagnostics need the plan too), so its
  // route only has to submit that plan.
  if (routeCandidate && decomposeSucceeded && submitFill(graph, plan)) {
    return true;
  }
  // Materialization retry, shared with on-screen draws (AOTRouter::RouteWithMaterializedLeading):
  // the caller's closure rebuilds the whole fill processor with materialized blend children. A
  // refused retry is dropped and the plain fill below keeps the ORIGINAL processor.
  if (retryWarranted && AOTRouter::RouteWithMaterializedLeading(
                            rebuildColorChain, {}, submitFill) == AOTRouteResult::Submitted) {
    return true;
  }
  auto allocator = drawingAllocator();
  auto drawOps = allocator->makeArray<DrawOp>(&drawOp, 1);
  auto textureProxy = renderTarget->asTextureProxy();
  // fillRTWithFP produces a new offscreen image, never a draw over prior target contents. Its
  // generated rectangle only covers the logical size, while the scratch backing may be larger.
  // Initialize the whole backing so downstream linear/decal sampling never sees stale texels.
  auto task = allocator->make<OpsRenderTask>(allocator, std::move(renderTarget), std::move(drawOps),
                                             PMColor::Transparent());
  addRenderTask(std::move(task));
  addGenerateMipmapsTask(std::move(textureProxy));
  return true;
}

std::shared_ptr<OpsCompositor> DrawingManager::addOpsCompositor(
    std::shared_ptr<RenderTargetProxy> target, uint32_t renderFlags,
    std::optional<PMColor> clearColor, std::shared_ptr<ColorSpace> colorSpace) {
  auto compositor = std::make_shared<OpsCompositor>(std::move(target), renderFlags, clearColor,
                                                    std::move(colorSpace));
  compositors.push_back(compositor);
  compositor->cachedPosition = --compositors.end();
  return compositor;
}

void DrawingManager::addOpsRenderTask(std::shared_ptr<RenderTargetProxy> renderTarget,
                                      PlacementArray<DrawOp> drawOps,
                                      std::optional<PMColor> clearColor) {
  if (renderTarget == nullptr || (drawOps.empty() && !clearColor.has_value())) {
    return;
  }
  auto drawingBuffer = getDrawingBuffer();
  auto allocator = &drawingBuffer->drawingAllocator;
  auto textureProxy = renderTarget->asTextureProxy();
  auto task = allocator->make<OpsRenderTask>(allocator, std::move(renderTarget), std::move(drawOps),
                                             clearColor);
  drawingBuffer->renderTasks.emplace_back(std::move(task));
  addGenerateMipmapsTask(std::move(textureProxy));
}

void DrawingManager::addRuntimeDrawTask(std::shared_ptr<RenderTargetProxy> renderTarget,
                                        std::vector<RuntimeInputTexture> inputs,
                                        std::shared_ptr<RuntimeEffect> effect,
                                        const Point& offset) {
  if (renderTarget == nullptr || inputs.empty() || effect == nullptr) {
    return;
  }
  auto drawingBuffer = getDrawingBuffer();
  auto allocator = &drawingBuffer->drawingAllocator;
  auto textureProxy = renderTarget->asTextureProxy();
  auto task = allocator->make<RuntimeDrawTask>(allocator, std::move(renderTarget),
                                               std::move(inputs), std::move(effect), offset);
  drawingBuffer->renderTasks.emplace_back(std::move(task));
  addGenerateMipmapsTask(std::move(textureProxy));
}

void DrawingManager::addGenerateMipmapsTask(std::shared_ptr<TextureProxy> textureProxy) {
  if (textureProxy == nullptr || !textureProxy->hasMipmaps()) {
    return;
  }
  auto drawingBuffer = getDrawingBuffer();
  auto allocator = &drawingBuffer->drawingAllocator;
  auto task = allocator->make<GenerateMipmapsTask>(allocator, std::move(textureProxy));
  drawingBuffer->renderTasks.emplace_back(std::move(task));
}

void DrawingManager::addRenderTargetCopyTask(std::shared_ptr<RenderTargetProxy> source,
                                             std::shared_ptr<TextureProxy> dest, int srcX,
                                             int srcY) {
  if (source == nullptr || dest == nullptr) {
    return;
  }
  auto drawingBuffer = getDrawingBuffer();
  auto allocator = &drawingBuffer->drawingAllocator;
  auto task = allocator->make<RenderTargetCopyTask>(allocator, std::move(source), std::move(dest),
                                                    srcX, srcY);
  drawingBuffer->renderTasks.emplace_back(std::move(task));
}

void DrawingManager::addTransferPixelsTask(std::shared_ptr<RenderTargetProxy> source,
                                           const Rect& srcRect,
                                           std::shared_ptr<GPUBufferProxy> dest) {
  if (source == nullptr || dest == nullptr || srcRect.isEmpty()) {
    return;
  }
  auto drawingBuffer = getDrawingBuffer();
  auto allocator = &drawingBuffer->drawingAllocator;
  auto task =
      allocator->make<TransferPixelsTask>(allocator, std::move(source), srcRect, std::move(dest));
  drawingBuffer->renderTasks.emplace_back(std::move(task));
}

void DrawingManager::addRenderTask(PlacementPtr<RenderTask> renderTask) {
  if (renderTask == nullptr) {
    return;
  }
  auto drawingBuffer = getDrawingBuffer();
  drawingBuffer->renderTasks.emplace_back(std::move(renderTask));
}

void DrawingManager::addResourceTask(PlacementPtr<ResourceTask> resourceTask) {
  if (resourceTask == nullptr) {
    return;
  }
  auto drawingBuffer = getDrawingBuffer();
  drawingBuffer->resourceTasks.emplace_back(std::move(resourceTask));
}

void DrawingManager::addAtlasCellTask(std::shared_ptr<TextureProxy> textureProxy,
                                      const Point& atlasOffset, std::shared_ptr<ImageCodec> codec) {
  if (textureProxy == nullptr || codec == nullptr) {
    return;
  }
  auto drawingBuffer = getDrawingBuffer();
  auto allocator = &drawingBuffer->drawingAllocator;
  AtlasUploadTask* atlasUploadTask = nullptr;
  auto taskKey = textureProxy.get();
  auto result = atlasTaskMap.find(taskKey);
  if (result != atlasTaskMap.end()) {
    atlasUploadTask = result->second;
  } else {
    auto atlasTask = AtlasUploadTask::Make(allocator, std::move(textureProxy));
    atlasUploadTask = atlasTask.get();
    drawingBuffer->atlasTasks.emplace_back(std::move(atlasTask));
    atlasTaskMap[taskKey] = atlasUploadTask;
  }
  atlasUploadTask->addCell(allocator, std::move(codec), atlasOffset);
}

void DrawingManager::collectWindow(std::weak_ptr<Window> window) {
  if (window.expired()) {
    return;
  }
  auto drawingBuffer = getDrawingBuffer();
  auto& windows = drawingBuffer->windows;
  for (const auto& w : windows) {
    if (!w.owner_before(window) && !window.owner_before(w)) {
      return;
    }
  }
  windows.push_back(std::move(window));
}

std::shared_ptr<DrawingBuffer> DrawingManager::flush() {
  if (currentBuffer == nullptr && compositors.empty()) {
    return nullptr;
  }
  if (currentBuffer == nullptr) {
    createDrawingBuffer();
  }
  // Settle the simultaneously-live materialized bytes before the recorded draws execute: the
  // offscreen textures built during this flush cycle (blend-child flattens, plan intermediates)
  // coexist until their consumers run, so the running total at this point is the cycle's
  // peak-live footprint. Diagnostic-only, and never influences routing.
  if (auto* statsCache = context->precompiledShaderCache();
      statsCache != nullptr && statsCache->diagnosticRecordingEnabled()) {
    statsCache->settleActiveTemporaryBytes();
  }
  while (!compositors.empty()) {
    auto compositor = compositors.back();
    // The makeClosed() method may add more compositors to the list.
    compositor->makeClosed();
  }
  // Flush the shared vertex buffer before executing the tasks. It may generate new resource tasks.
  context->proxyProvider()->flushSharedVertexBuffer();
  context->proxyProvider()->flushSharedInstanceBuffer();
  atlasTaskMap.clear();

  if (currentBuffer->empty()) {
    currentBuffer->reset();
    return nullptr;
  }

  auto drawingBuffer = currentBuffer;
  bufferPool.push_back(currentBuffer);
  currentBuffer = nullptr;
  return drawingBuffer;
}
}  // namespace tgfx

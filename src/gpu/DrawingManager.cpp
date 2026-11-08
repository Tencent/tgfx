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
#include "gpu/ContextInternal.h"
#include "gpu/aot/AOTRouter.h"
#include "gpu/aot/ir/DrawIR.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "gpu/proxies/TextureProxy.h"
#include "gpu/tasks/GenerateMipmapsTask.h"
#include "gpu/tasks/RenderTargetCopyTask.h"
#include "gpu/tasks/RuntimeDrawTask.h"
#include "gpu/tasks/TextureCopyTask.h"
#include "tasks/TransferPixelsTask.h"
#include "tgfx/gpu/Window.h"

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

void DrawingManager::collectFrameUsage(std::shared_ptr<RenderTargetProxy> renderTarget) {
  if (renderTarget == nullptr) {
    return;
  }
  auto drawingBuffer = getDrawingBuffer();
  for (auto& proxy : drawingBuffer->windowUsages) {
    if (proxy == renderTarget) {
      return;
    }
  }
  drawingBuffer->windowUsages.push_back(std::move(renderTarget));
}

PlacementPtr<DrawOp> DrawingManager::makePassFillDrawOp(
    std::shared_ptr<RenderTargetProxy> renderTarget, PlacementPtr<DrawIR> passIR,
    uint32_t renderFlags, const Point& coordOffset, float inputAlpha) {
  if (passIR == nullptr) {
    return nullptr;
  }
  auto drawOp = makeFillDrawOp(renderTarget, PlacementPtr<FragmentProcessor>(), renderFlags,
                               coordOffset, inputAlpha);
  if (drawOp == nullptr) {
    return nullptr;
  }
  drawOp->adoptLoweredIR(std::move(passIR));
  return drawOp;
}

PlacementPtr<DrawOp> DrawingManager::makeFillDrawOp(std::shared_ptr<RenderTargetProxy> renderTarget,
                                                    PlacementPtr<FragmentProcessor> processor,
                                                    uint32_t renderFlags, const Point& coordOffset,
                                                    float inputAlpha) {
  if (renderTarget == nullptr) {
    return nullptr;
  }
  // A null processor is only passed by the plan-pass overload above, which attaches the pass IR
  // in place of a color processor.
  auto allocator = drawingAllocator();
  auto width = static_cast<float>(renderTarget->width());
  auto height = static_cast<float>(renderTarget->height());
  PlacementPtr<RectsVertexProvider> provider = nullptr;
  if (coordOffset.x == 0.0f && coordOffset.y == 0.0f && inputAlpha == 1.0f) {
    auto bounds = Rect::MakeWH(width, height);
    provider = RectsVertexProvider::MakeFrom(allocator, bounds, AAType::None, true);
  } else {
    auto rect = Rect::MakeXYWH(coordOffset.x, coordOffset.y, width, height);
    auto viewMatrix = Matrix::MakeTrans(-coordOffset.x, -coordOffset.y);
    // The first split-off pass must see the original draw's input alpha before its pointwise
    // operators run. For later passes this remains 1: their texture leaf is unmodulated.
    auto record = allocator->make<RectRecord>(rect, viewMatrix, Color(1, 1, 1, inputAlpha));
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
  if (processor != nullptr) {
    drawOp->addColorFP(std::move(processor));
  }
  drawOp->setBlendMode(BlendMode::Src);
  return drawOp;
}

bool DrawingManager::fillRTWithFP(std::shared_ptr<RenderTargetProxy> renderTarget,
                                  PlacementPtr<FragmentProcessor> processor, uint32_t renderFlags,
                                  const Point& coordOffset) {
  const FragmentProcessor* fillProcessor = processor.get();
  auto drawOp = makeFillDrawOp(renderTarget, std::move(processor), renderFlags, coordOffset);
  if (drawOp == nullptr) {
    return false;
  }
  // The record-time AOT route's one entry (see AOTRouter::route): the fill's IR is unconditional
  // and a plan task takes ownership of the draw op on success, so the fill must stop there —
  // falling through to the plain branch would submit a null op. An offscreen fill owns a fresh
  // target, so the task clears its full backing first and a shader-side sample cannot observe
  // pixels left by an earlier scratch allocation. A single-pass chain-capable plan needs no task:
  // the fill's RectDrawOp is a StandardDrawOp, so the plain lookup serves it at prepare time.
  AOTRouteRequest request = {};
  request.kind = AOTRouteRequest::Kind::Fill;
  request.target = renderTarget;
  request.coordOffset = coordOffset;
  request.renderFlags = renderFlags;
  request.clearDestination = true;
  request.processor = fillProcessor;
  AOTRouter router(context);
  if (auto task = router.route(&drawOp, request)) {
    addRenderTask(std::move(task));
    addGenerateMipmapsTask(renderTarget->asTextureProxy());
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
  // Ops submitted as a batch (a clip's mask layer, a layer's own render) never pass through
  // OpsCompositor::addDrawOp, so this is their record-time lowering (see AOTRouter::lowerOps):
  // the IR exists before the task prepares them, exactly like a compositor-recorded draw.
  AOTRouter router(context);
  router.lowerOps(drawOps);
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

void DrawingManager::addTextureCopyTask(std::shared_ptr<TextureProxy> source,
                                        std::shared_ptr<TextureProxy> dest) {
  if (source == nullptr || dest == nullptr) {
    return;
  }
  auto drawingBuffer = getDrawingBuffer();
  auto allocator = &drawingBuffer->drawingAllocator;
  auto task = allocator->make<TextureCopyTask>(allocator, std::move(source), std::move(dest));
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

DrawingManager::WindowTarget& DrawingManager::windowTargetEntry(
    const std::shared_ptr<Window>& window) {
  auto& entry = windowTargets[window.get()];
  if (entry.window.lock() != window) {
    // A new window reusing the address of a destroyed one; its old target is unrelated.
    entry = {window, {}};
  }
  return entry;
}

std::shared_ptr<RenderTargetProxy> DrawingManager::createWindowTarget(
    std::shared_ptr<Window> window) {
  if (window == nullptr) {
    return nullptr;
  }
  auto& entry = windowTargetEntry(window);
  auto current = entry.target.lock();
  if (current != nullptr && isFrameUsed(current)) {
    // The current batch already uses the window's frame; every surface joins it.
    return current;
  }
  auto target = window->onCreateRenderTarget(context);
  if (target == nullptr) {
    return nullptr;
  }
  // Lets the proxy present through the window and detect when the window is gone.
  target->setPresentingWindow(window);
  entry.target = target;
  return target;
}

std::shared_ptr<RenderTargetProxy> DrawingManager::resolveWindowTarget(
    const std::shared_ptr<Window>& window, std::shared_ptr<RenderTargetProxy> renderTarget) {
  if (window == nullptr || renderTarget == nullptr) {
    return renderTarget;
  }
  auto& entry = windowTargetEntry(window);
  auto current = entry.target.lock();
  if (current == nullptr) {
    // No live surface holds a newer target of the window; the caller's target becomes current.
    entry.target = renderTarget;
    return renderTarget;
  }
  return current;
}

bool DrawingManager::isFrameUsed(const std::shared_ptr<RenderTargetProxy>& renderTarget) const {
  if (currentBuffer == nullptr || renderTarget == nullptr) {
    return false;
  }
  for (auto& proxy : currentBuffer->windowUsages) {
    if (proxy == renderTarget) {
      return true;
    }
  }
  return false;
}

void DrawingManager::closeCompositors(const std::shared_ptr<RenderTargetProxy>& renderTarget) {
  if (renderTarget == nullptr) {
    return;
  }
  // Collect first: makeClosed() removes the compositor from the list.
  std::vector<std::shared_ptr<OpsCompositor>> openCompositors = {};
  for (auto& compositor : compositors) {
    if (compositor->renderTarget == renderTarget) {
      openCompositors.push_back(compositor);
    }
  }
  for (auto& compositor : openCompositors) {
    compositor->makeClosed();
  }
}

void DrawingManager::collectWindow(std::shared_ptr<Window> window,
                                   std::shared_ptr<RenderTargetProxy> renderTarget) {
  if (window == nullptr || renderTarget == nullptr) {
    return;
  }
  auto drawingBuffer = getDrawingBuffer();
  // Present each window target once per batch.
  for (auto& pending : drawingBuffer->presentables) {
    if (pending == renderTarget) {
      return;
    }
  }
  drawingBuffer->presentables.push_back(std::move(renderTarget));
  pendingWindows.push_back(std::move(window));
}

bool DrawingManager::hasPendingPresentation(
    const std::shared_ptr<RenderTargetProxy>& renderTarget) const {
  if (currentBuffer == nullptr || renderTarget == nullptr) {
    return false;
  }
  for (auto& pending : currentBuffer->presentables) {
    if (pending == renderTarget) {
      return true;
    }
  }
  return false;
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
  if (auto* statsCache = ContextCacheAccess::Cache(context);
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
  for (auto iter = windowTargets.begin(); iter != windowTargets.end();) {
    if (iter->second.window.expired() || iter->second.target.expired()) {
      iter = windowTargets.erase(iter);
    } else {
      ++iter;
    }
  }
  return drawingBuffer;
}
}  // namespace tgfx

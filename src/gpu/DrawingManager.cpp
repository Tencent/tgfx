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
#include <cstdio>
#include "ProxyProvider.h"
#include "core/AtlasManager.h"
#include "core/utils/GpuProbe.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "gpu/proxies/TextureProxy.h"
#include "gpu/tasks/GenerateMipmapsTask.h"
#include "gpu/tasks/RenderTargetCopyTask.h"
#include "gpu/tasks/RuntimeDrawTask.h"
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

bool DrawingManager::fillRTWithFP(std::shared_ptr<RenderTargetProxy> renderTarget,
                                  PlacementPtr<FragmentProcessor> processor, uint32_t renderFlags) {
  if (renderTarget == nullptr || processor == nullptr) {
    return false;
  }
  auto drawingBuffer = getDrawingBuffer();
  auto allocator = &drawingBuffer->drawingAllocator;
  auto bounds = Rect::MakeWH(renderTarget->width(), renderTarget->height());
  auto provider = RectsVertexProvider::MakeFrom(allocator, bounds, AAType::None);
  auto drawOp = RectDrawOp::Make(renderTarget->getContext(), std::move(provider), renderFlags);
  drawOp->addColorFP(std::move(processor));
  drawOp->setBlendMode(BlendMode::Src);
  auto drawOps = allocator->makeArray<DrawOp>(&drawOp, 1);
  auto textureProxy = renderTarget->asTextureProxy();
  std::optional<PMColor> clearColor = std::nullopt;
  if (textureProxy != nullptr && textureProxy->hasMipmaps() &&
      (textureProxy->backingStoreWidth() != textureProxy->width() ||
       textureProxy->backingStoreHeight() != textureProxy->height())) {
    // Mipmap generation samples the whole backing store, including unwritten padding.
    clearColor = PMColor::Transparent();
  }
  ++gGpuProbe.passes;
  gGpuProbe.passPx += static_cast<double>(renderTarget->width()) * renderTarget->height();
  ++gGpuProbe.drawOps;
  ++gGpuProbe.passesPerTarget[renderTarget.get()];
  auto task = allocator->make<OpsRenderTask>(allocator, std::move(renderTarget), std::move(drawOps),
                                             clearColor);
  drawingBuffer->renderTasks.emplace_back(std::move(task));
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
  ++gGpuProbe.passes;
  gGpuProbe.passPx += static_cast<double>(renderTarget->width()) * renderTarget->height();
  gGpuProbe.drawOps += static_cast<int>(drawOps.size());
  ++gGpuProbe.passesPerTarget[renderTarget.get()];
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
  ++gGpuProbe.runtimeTasks;
  ++gGpuProbe.passesPerTarget[renderTarget.get()];
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
  ++gGpuProbe.mipmapTasks;
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
  ++gGpuProbe.copyTasks;
  gGpuProbe.copyPx += static_cast<double>(dest->width()) * dest->height();
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
  if (currentBuffer == nullptr) {
    return nullptr;
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
  if (gGpuProbe.passes > 0 || gGpuProbe.copyTasks > 0) {
    auto& p = gGpuProbe;
    int splitTargets = 0;
    int extraPasses = 0;
    for (const auto& entry : p.passesPerTarget) {
      if (entry.second > 1) {
        ++splitTargets;
        extraPasses += entry.second - 1;
      }
    }
    std::printf(
        "[GPU-FLUSH] passes=%d passMpx=%.2f ops=%d targets=%zu splitTargets=%d extraPasses=%d "
        "runtime=%d mipmaps=%d copies=%d copyMpx=%.2f cow=%d cowMpx=%.2f newRT=%d newRTMpx=%.2f "
        "blurs=%d blurMpx=%.2f rasterized=%d snapshots=%d\n",
        p.passes, p.passPx / 1e6, p.drawOps, p.passesPerTarget.size(), splitTargets, extraPasses,
        p.runtimeTasks, p.mipmapTasks, p.copyTasks, p.copyPx / 1e6, p.cow, p.cowPx / 1e6,
        p.newTargets, p.newTargetPx / 1e6, p.blurs, p.blurPx / 1e6, p.rasterized, p.snapshots);
  }
  gGpuProbe.reset();

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

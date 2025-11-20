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
#include "core/utils/HardwareBufferUtil.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "gpu/proxies/TextureProxy.h"
#include "gpu/tasks/GenerateMipmapsTask.h"
#include "gpu/tasks/RenderTargetCopyTask.h"
#include "gpu/tasks/RuntimeDrawTask.h"
#include "inspect/InspectorMark.h"
#include "tasks/TransferPixelsTask.h"
#include "tgfx/gpu/GPU.h"

namespace tgfx {
DrawingManager::DrawingManager(Context* context)
    : context(context), drawingAllocator(context->drawingAllocator()) {
}

bool DrawingManager::fillRTWithFP(std::shared_ptr<RenderTargetProxy> renderTarget,
                                  PlacementPtr<FragmentProcessor> processor, uint32_t renderFlags) {
  if (renderTarget == nullptr || processor == nullptr) {
    return false;
  }
  auto bounds = Rect::MakeWH(renderTarget->width(), renderTarget->height());
  auto provider = RectsVertexProvider::MakeFrom(drawingAllocator, bounds, AAType::None);
  auto drawOp = RectDrawOp::Make(renderTarget->getContext(), std::move(provider), renderFlags);
  drawOp->addColorFP(std::move(processor));
  drawOp->setBlendMode(BlendMode::Src);
  auto drawOps = drawingAllocator->makeArray<DrawOp>(&drawOp, 1);
  auto textureProxy = renderTarget->asTextureProxy();
  auto task = drawingAllocator->make<OpsRenderTask>(drawingAllocator, std::move(renderTarget),
                                                    std::move(drawOps), std::nullopt);
  renderTasks.emplace_back(std::move(task));
  addGenerateMipmapsTask(std::move(textureProxy));
  return true;
}

std::shared_ptr<OpsCompositor> DrawingManager::addOpsCompositor(
    std::shared_ptr<RenderTargetProxy> target, uint32_t renderFlags,
    std::optional<Color> clearColor, std::shared_ptr<ColorSpace> colorSpace) {
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
  auto textureProxy = renderTarget->asTextureProxy();
  auto task = drawingAllocator->make<OpsRenderTask>(drawingAllocator, std::move(renderTarget),
                                                    std::move(drawOps), clearColor);
  renderTasks.emplace_back(std::move(task));
  addGenerateMipmapsTask(std::move(textureProxy));
}

void DrawingManager::addRuntimeDrawTask(std::shared_ptr<RenderTargetProxy> renderTarget,
                                        std::vector<RuntimeInputTexture> inputs,
                                        std::shared_ptr<RuntimeEffect> effect,
                                        const Point& offset) {
  if (renderTarget == nullptr || inputs.empty() || effect == nullptr) {
    return;
  }
  auto textureProxy = renderTarget->asTextureProxy();
  auto task = drawingAllocator->make<RuntimeDrawTask>(drawingAllocator, std::move(renderTarget),
                                                      std::move(inputs), std::move(effect), offset);
  renderTasks.emplace_back(std::move(task));
  addGenerateMipmapsTask(std::move(textureProxy));
}

void DrawingManager::addGenerateMipmapsTask(std::shared_ptr<TextureProxy> textureProxy) {
  if (textureProxy == nullptr || !textureProxy->hasMipmaps()) {
    return;
  }
  auto task =
      drawingAllocator->make<GenerateMipmapsTask>(drawingAllocator, std::move(textureProxy));
  renderTasks.emplace_back(std::move(task));
}

void DrawingManager::addRenderTargetCopyTask(std::shared_ptr<RenderTargetProxy> source,
                                             std::shared_ptr<TextureProxy> dest, int srcX,
                                             int srcY) {
  if (source == nullptr || dest == nullptr) {
    return;
  }
  auto task = drawingAllocator->make<RenderTargetCopyTask>(drawingAllocator, std::move(source),
                                                           std::move(dest), srcX, srcY);
  renderTasks.emplace_back(std::move(task));
}

void DrawingManager::addTransferPixelsTask(std::shared_ptr<RenderTargetProxy> source,
                                           const Rect& srcRect,
                                           std::shared_ptr<GPUBufferProxy> dest) {
  if (source == nullptr || dest == nullptr || srcRect.isEmpty()) {
    return;
  }
  auto task = drawingAllocator->make<TransferPixelsTask>(drawingAllocator, std::move(source),
                                                         srcRect, std::move(dest));
  renderTasks.emplace_back(std::move(task));
}

void DrawingManager::addResourceTask(PlacementPtr<ResourceTask> resourceTask) {
  if (resourceTask == nullptr) {
    return;
  }
  resourceTasks.emplace_back(std::move(resourceTask));
}

void DrawingManager::addAtlasCellTask(std::shared_ptr<TextureProxy> textureProxy,
                                      const Point& atlasOffset, std::shared_ptr<ImageCodec> codec) {
  if (textureProxy == nullptr || codec == nullptr) {
    return;
  }
  AtlasUploadTask* atlasUploadTask = nullptr;
  auto result = atlasTasks.find(textureProxy.get());
  if (result != atlasTasks.end()) {
    atlasUploadTask = result->second.get();
  } else {
    auto atlasTask = drawingAllocator->make<AtlasUploadTask>(textureProxy);
    atlasUploadTask = atlasTask.get();
    atlasTasks[textureProxy.get()] = std::move(atlasTask);
  }
  atlasUploadTask->addCell(drawingAllocator, std::move(codec), atlasOffset);
}

std::shared_ptr<CommandBuffer> DrawingManager::flush() {
  TASK_MARK(tgfx::inspect::OpTaskType::Flush);
  while (!compositors.empty()) {
    auto compositor = compositors.back();
    // The makeClosed() method may add more compositors to the list.
    compositor->makeClosed();
  }
  auto proxyProvider = context->proxyProvider();
  // Flush the shared vertex buffer before executing the tasks. It may generate new resource tasks.
  proxyProvider->flushSharedVertexBuffer();

  if (resourceTasks.empty() && renderTasks.empty() && atlasTasks.empty()) {
    proxyProvider->clearSharedVertexBuffer();
    return nullptr;
  }
  {
    TASK_MARK(tgfx::inspect::OpTaskType::ResourceTask);
    for (auto& task : resourceTasks) {
      task->execute(context);
      task = nullptr;
    }
  }
  resourceTasks.clear();
  for (auto& task : atlasTasks) {
    task.second->upload(context);
  }
  atlasTasks.clear();
  proxyProvider->clearSharedVertexBuffer();
  auto commandEncoder = context->gpu()->createCommandEncoder();
  {
    TASK_MARK(tgfx::inspect::OpTaskType::RenderTask);
    for (auto& task : renderTasks) {
      task->execute(commandEncoder.get());
      task = nullptr;
    }
  }
  renderTasks.clear();
  return commandEncoder->finish();
}
}  // namespace tgfx

/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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
#include "gpu/proxies/RenderTargetProxy.h"
#include "gpu/proxies/TextureProxy.h"
#include "gpu/tasks/RenderTargetCopyTask.h"
#include "gpu/tasks/RuntimeDrawTask.h"
#include "gpu/tasks/TextureResolveTask.h"

namespace tgfx {
DrawingManager::DrawingManager(Context* context)
    : context(context), drawingBuffer(context->drawingBuffer()) {
}

bool DrawingManager::fillRTWithFP(std::shared_ptr<RenderTargetProxy> renderTarget,
                                  std::unique_ptr<FragmentProcessor> processor,
                                  uint32_t renderFlags) {
  if (renderTarget == nullptr || processor == nullptr) {
    return false;
  }
  auto bounds = Rect::MakeWH(renderTarget->width(), renderTarget->height());
  auto rectPaint = drawingBuffer->makeNode<RectPaint>(bounds, Matrix::I());
  auto op = RectDrawOp::Make(renderTarget->getContext(), {std::move(rectPaint)}, true, AAType::None,
                             renderFlags);
  op->addColorFP(std::move(processor));
  op->setBlendMode(BlendMode::Src);
  PlacementList<Op> ops(std::move(op));
  auto task = drawingBuffer->makeNode<OpsRenderTask>(renderTarget, std::move(ops));
  renderTasks.append(std::move(task));
  addTextureResolveTask(std::move(renderTarget));
  return true;
}

std::shared_ptr<OpsCompositor> DrawingManager::addOpsCompositor(
    std::shared_ptr<RenderTargetProxy> target, uint32_t renderFlags) {
  auto compositor = std::make_shared<OpsCompositor>(std::move(target), renderFlags);
  compositors.push_back(compositor);
  compositor->cachedPosition = --compositors.end();
  return compositor;
}

void DrawingManager::addOpsRenderTask(std::shared_ptr<RenderTargetProxy> renderTarget,
                                      PlacementList<Op> ops) {
  if (renderTarget == nullptr || ops.empty()) {
    return;
  }
  auto task = drawingBuffer->makeNode<OpsRenderTask>(renderTarget, std::move(ops));
  renderTasks.append(std::move(task));
  addTextureResolveTask(std::move(renderTarget));
}

void DrawingManager::addRuntimeDrawTask(std::shared_ptr<RenderTargetProxy> renderTarget,
                                        std::vector<std::shared_ptr<TextureProxy>> inputs,
                                        std::shared_ptr<RuntimeEffect> effect,
                                        const Point& offset) {
  if (renderTarget == nullptr || inputs.empty() || effect == nullptr) {
    return;
  }
  auto task = drawingBuffer->makeNode<RuntimeDrawTask>(renderTarget, std::move(inputs),
                                                       std::move(effect), offset);
  renderTasks.append(std::move(task));
  addTextureResolveTask(std::move(renderTarget));
}

void DrawingManager::addTextureResolveTask(std::shared_ptr<RenderTargetProxy> target) {
  auto textureProxy = target->getTextureProxy();
  if (textureProxy == nullptr || (target->sampleCount() <= 1 && !textureProxy->hasMipmaps())) {
    return;
  }
  auto task = drawingBuffer->makeNode<TextureResolveTask>(std::move(target));
  renderTasks.append(std::move(task));
}

void DrawingManager::addTextureFlattenTask(UniqueKey uniqueKey,
                                           std::shared_ptr<TextureProxy> textureProxy) {
  if (textureProxy == nullptr) {
    return;
  }
  auto task =
      drawingBuffer->makeNode<TextureFlattenTask>(std::move(uniqueKey), std::move(textureProxy));
  flattenTasks.append(std::move(task));
}

void DrawingManager::addRenderTargetCopyTask(std::shared_ptr<RenderTargetProxy> source,
                                             std::shared_ptr<TextureProxy> dest) {
  if (source == nullptr || dest == nullptr) {
    return;
  }
  DEBUG_ASSERT(source->width() == dest->width() && source->height() == dest->height());
  auto task = drawingBuffer->makeNode<RenderTargetCopyTask>(std::move(source), std::move(dest));
  renderTasks.append(std::move(task));
}

void DrawingManager::addResourceTask(PlacementNode<ResourceTask> resourceTask) {
  if (resourceTask == nullptr) {
    return;
  }
  auto result = resourceTaskMap.find(resourceTask->uniqueKey);
  if (result != resourceTaskMap.end()) {
    // Remove the unique key from the old task, so it will be skipped when the task is executed.
    result->second->uniqueKey = {};
  } else {
    resourceTaskMap[resourceTask->uniqueKey] = resourceTask.get();
    resourceTasks.append(std::move(resourceTask));
  }
}

bool DrawingManager::flush() {
  while (!compositors.empty()) {
    auto compositor = compositors.back();
    // The makeClosed() method may add more compositors to the list.
    compositor->makeClosed();
  }

  if (resourceTasks.empty() && renderTasks.empty()) {
    return false;
  }
  for (auto& task : resourceTasks) {
    task.execute(context);
  }
  resourceTasks.clear();
  resourceTaskMap = {};

  if (renderPass == nullptr) {
    renderPass = RenderPass::Make(context);
  }
  std::vector<TextureFlattenTask*> validFlattenTasks = {};
  validFlattenTasks.reserve(flattenTasks.size());
  for (auto& task : flattenTasks) {
    if (task.prepare(context)) {
      validFlattenTasks.push_back(&task);
    }
  }
  for (auto& task : validFlattenTasks) {
    task->execute(renderPass.get());
  }
  flattenTasks.clear();
  for (auto& task : renderTasks) {
    task.execute(renderPass.get());
  }
  renderTasks.clear();
  return true;
}
}  // namespace tgfx

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
bool DrawingManager::fillRTWithFP(std::shared_ptr<RenderTargetProxy> renderTarget,
                                  std::unique_ptr<FragmentProcessor> processor,
                                  uint32_t renderFlags) {
  if (renderTarget == nullptr || processor == nullptr) {
    return false;
  }
  auto bounds = Rect::MakeWH(renderTarget->width(), renderTarget->height());
  RectPaint rectPaint = {bounds, Matrix::I()};
  auto op =
      RectDrawOp::Make(renderTarget->getContext(), {rectPaint}, true, AAType::None, renderFlags);
  op->addColorFP(std::move(processor));
  op->setBlendMode(BlendMode::Src);
  std::vector<std::unique_ptr<Op>> ops = {};
  ops.push_back(std::move(op));
  auto opsTask = std::make_unique<OpsRenderTask>(renderTarget, std::move(ops));
  renderTasks.push_back(std::move(opsTask));
  addTextureResolveTask(std::move(renderTarget));
  return true;
}

std::shared_ptr<OpsCompositor> DrawingManager::addOpsCompositor(
    std::shared_ptr<RenderTargetProxy> target, uint32_t renderFlags) {
  auto compositor = std::make_shared<OpsCompositor>(this, std::move(target), renderFlags);
  compositors.push_back(compositor);
  return compositor;
}

void DrawingManager::addOpsRenderTask(std::shared_ptr<RenderTargetProxy> renderTarget,
                                      std::vector<std::unique_ptr<Op>> ops) {
  if (renderTarget == nullptr || ops.empty()) {
    return;
  }
  auto renderTask = std::make_unique<OpsRenderTask>(renderTarget, std::move(ops));
  renderTasks.push_back(std::move(renderTask));
  addTextureResolveTask(std::move(renderTarget));
}

void DrawingManager::addRuntimeDrawTask(std::shared_ptr<RenderTargetProxy> renderTarget,
                                        std::vector<std::shared_ptr<TextureProxy>> inputs,
                                        std::shared_ptr<RuntimeEffect> effect,
                                        const Point& offset) {
  if (renderTarget == nullptr || inputs.empty() || effect == nullptr) {
    return;
  }
  auto task =
      std::make_unique<RuntimeDrawTask>(renderTarget, std::move(inputs), std::move(effect), offset);
  renderTasks.push_back(std::move(task));
  addTextureResolveTask(std::move(renderTarget));
}

void DrawingManager::addTextureResolveTask(std::shared_ptr<RenderTargetProxy> target) {
  auto textureProxy = target->getTextureProxy();
  if (textureProxy == nullptr || (target->sampleCount() <= 1 && !textureProxy->hasMipmaps())) {
    return;
  }
  auto task = std::make_unique<TextureResolveTask>(std::move(target));
  renderTasks.push_back(std::move(task));
}

void DrawingManager::addTextureFlattenTask(std::unique_ptr<TextureFlattenTask> flattenTask) {
  if (flattenTask == nullptr) {
    return;
  }
  flattenTasks.push_back(std::move(flattenTask));
}

void DrawingManager::addRenderTargetCopyTask(std::shared_ptr<RenderTargetProxy> source,
                                             std::shared_ptr<TextureProxy> dest) {
  if (source == nullptr || dest == nullptr) {
    return;
  }
  DEBUG_ASSERT(source->width() == dest->width() && source->height() == dest->height());
  auto task = std::make_unique<RenderTargetCopyTask>(std::move(source), std::move(dest));
  renderTasks.push_back(std::move(task));
}

void DrawingManager::addResourceTask(std::unique_ptr<ResourceTask> resourceTask) {
  if (resourceTask == nullptr) {
    return;
  }
  auto result = resourceTaskMap.find(resourceTask->uniqueKey);
  if (result != resourceTaskMap.end()) {
    // Replace the existing task with the new one.
    resourceTasks[result->second] = std::move(resourceTask);
  } else {
    resourceTaskMap[resourceTask->uniqueKey] = resourceTasks.size();
    resourceTasks.push_back(std::move(resourceTask));
  }
}

template <typename T>
void ClearAndReserveSize(std::vector<T>& list) {
  auto size = list.size();
  auto capacity = list.capacity();
  auto nextSize = (size << 1) - (size >> 1);  // 1.5 * size
  if (capacity > nextSize) {
    list = std::vector<T>{};
    list.reserve(size);
  } else {
    list.clear();
  }
}

bool DrawingManager::flush() {
  while (!compositors.empty()) {
    auto compositor = compositors.back();
    compositors.pop_back();
    // the makeClosed() method may add more compositors to the list.
    compositor->makeClosed();
  }

  if (resourceTasks.empty() && renderTasks.empty()) {
    return false;
  }
  for (auto& task : resourceTasks) {
    task->execute(context);
  }
  ClearAndReserveSize(resourceTasks);
  resourceTaskMap = {};

  if (renderPass == nullptr) {
    renderPass = RenderPass::Make(context);
  }
  std::vector<TextureFlattenTask*> validFlattenTasks = {};
  validFlattenTasks.reserve(flattenTasks.size());
  for (auto& task : flattenTasks) {
    if (task->prepare(context)) {
      validFlattenTasks.push_back(task.get());
    }
  }
  for (auto& task : validFlattenTasks) {
    task->execute(renderPass.get());
  }
  ClearAndReserveSize(flattenTasks);
  for (auto& task : renderTasks) {
    task->execute(renderPass.get());
  }
  ClearAndReserveSize(renderTasks);
  return true;
}
}  // namespace tgfx

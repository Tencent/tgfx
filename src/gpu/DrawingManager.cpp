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
#include "core/AtlasCellDecodeTask.h"
#include "core/AtlasManager.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "gpu/proxies/TextureProxy.h"
#include "gpu/tasks/RenderTargetCopyTask.h"
#include "gpu/tasks/RuntimeDrawTask.h"
#include "gpu/tasks/TextureResolveTask.h"

namespace tgfx {
static ColorType GetAtlasColorType(bool isAplhaOnly) {
  if (isAplhaOnly) {
    return ColorType::ALPHA_8;
  }
#ifdef __APPLE__
  return ColorType::BGRA_8888;
#else
  return ColorType::RGBA_8888;
#endif
}

DrawingManager::DrawingManager(Context* context)
    : context(context), drawingBuffer(context->drawingBuffer()) {
}

bool DrawingManager::fillRTWithFP(std::shared_ptr<RenderTargetProxy> renderTarget,
                                  PlacementPtr<FragmentProcessor> processor, uint32_t renderFlags) {
  if (renderTarget == nullptr || processor == nullptr) {
    return false;
  }
  auto bounds = Rect::MakeWH(renderTarget->width(), renderTarget->height());
  auto provider = RectsVertexProvider::MakeFrom(drawingBuffer, bounds, AAType::None);
  auto op = RectDrawOp::Make(renderTarget->getContext(), std::move(provider), renderFlags);
  op->addColorFP(std::move(processor));
  op->setBlendMode(BlendMode::Src);
  auto ops = drawingBuffer->makeArray<Op>(&op, 1);
  auto task = drawingBuffer->make<OpsRenderTask>(renderTarget, std::move(ops));
  renderTasks.emplace_back(std::move(task));
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
                                      PlacementArray<Op> ops) {
  if (renderTarget == nullptr || ops.empty()) {
    return;
  }
  auto task = drawingBuffer->make<OpsRenderTask>(renderTarget, std::move(ops));
  renderTasks.emplace_back(std::move(task));
  addTextureResolveTask(std::move(renderTarget));
}

void DrawingManager::addRuntimeDrawTask(std::shared_ptr<RenderTargetProxy> renderTarget,
                                        std::vector<std::shared_ptr<TextureProxy>> inputs,
                                        std::shared_ptr<RuntimeEffect> effect,
                                        const Point& offset) {
  if (renderTarget == nullptr || inputs.empty() || effect == nullptr) {
    return;
  }
  auto task = drawingBuffer->make<RuntimeDrawTask>(renderTarget, std::move(inputs),
                                                   std::move(effect), offset);
  renderTasks.emplace_back(std::move(task));
  addTextureResolveTask(std::move(renderTarget));
}

void DrawingManager::addTextureResolveTask(std::shared_ptr<RenderTargetProxy> target) {
  auto textureProxy = target->getTextureProxy();
  if (textureProxy == nullptr || (target->sampleCount() <= 1 && !textureProxy->hasMipmaps())) {
    return;
  }
  auto task = drawingBuffer->make<TextureResolveTask>(std::move(target));
  renderTasks.emplace_back(std::move(task));
}

void DrawingManager::addTextureFlattenTask(UniqueKey uniqueKey,
                                           std::shared_ptr<TextureProxy> textureProxy) {
  if (textureProxy == nullptr) {
    return;
  }
  auto task =
      drawingBuffer->make<TextureFlattenTask>(std::move(uniqueKey), std::move(textureProxy));
  flattenTasks.emplace_back(std::move(task));
}

void DrawingManager::addRenderTargetCopyTask(std::shared_ptr<RenderTargetProxy> source,
                                             std::shared_ptr<TextureProxy> dest) {
  if (source == nullptr || dest == nullptr) {
    return;
  }
  DEBUG_ASSERT(source->width() == dest->width() && source->height() == dest->height());
  auto task = drawingBuffer->make<RenderTargetCopyTask>(std::move(source), std::move(dest));
  renderTasks.emplace_back(std::move(task));
}

void DrawingManager::addResourceTask(PlacementPtr<ResourceTask> resourceTask) {
  if (resourceTask == nullptr) {
    return;
  }
  auto result = resourceTaskMap.find(resourceTask->uniqueKey);
  if (result != resourceTaskMap.end()) {
    // Remove the unique key from the old task, so it will be skipped when the task is executed.
    result->second->uniqueKey = {};
  }
  resourceTaskMap[resourceTask->uniqueKey] = resourceTask.get();
  resourceTasks.emplace_back(std::move(resourceTask));
}

bool DrawingManager::flush() {
  while (!compositors.empty()) {
    auto compositor = compositors.back();
    // The makeClosed() method may add more compositors to the list.
    compositor->makeClosed();
  }
  auto proxyProvider = context->proxyProvider();
  // Flush the shared vertex buffer before executing the tasks. It may generate new resource tasks.
  proxyProvider->flushSharedVertexBuffer();

  if (resourceTasks.empty() && renderTasks.empty()) {
    proxyProvider->clearSharedVertexBuffer();
    clearAtlasCellCodecTasks();
    return false;
  }
  for (auto& task : resourceTasks) {
    task->execute(context);
  }
  uploadAtlasToGPU();
  resourceTasks.clear();
  resourceTaskMap = {};
  proxyProvider->clearSharedVertexBuffer();

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
  flattenTasks.clear();
  for (auto& task : renderTasks) {
    task->execute(renderPass.get());
  }
  renderTasks.clear();
  return true;
}

void DrawingManager::releaseAll() {
  compositors.clear();
  resourceTasks.clear();
  resourceTaskMap = {};
  flattenTasks.clear();
  renderTasks.clear();
  atlasCellCodecTasks.clear();
  atlasCellDatas.clear();
}

void DrawingManager::addAtlasCellCodecTask(const std::shared_ptr<TextureProxy>& textureProxy,
                                           const Point& atlasOffset,
                                           std::shared_ptr<ImageCodec> codec) {
  if (textureProxy == nullptr || codec == nullptr) {
    return;
  }
  auto padding = Plot::CellPadding;
  auto colorType = GetAtlasColorType(codec->isAlphaOnly());
  auto dstInfo =
      ImageInfo::Make(codec->width() + 2 * padding, codec->height() + 2 * padding, colorType);
  auto length = dstInfo.byteSize();
  auto buffer = new (std::nothrow) uint8_t[length];
  if (buffer == nullptr) {
    return;
  }
  auto data = Data::MakeAdopted(buffer, length, Data::DeleteProc);
  auto uploadOffset = atlasOffset;
  auto floatPadding = static_cast<float>(padding);
  uploadOffset.offset(-floatPadding, -floatPadding);
  atlasCellDatas[textureProxy].emplace_back(std::move(data), dstInfo, uploadOffset);
  auto task = std::make_shared<AtlasCellDecodeTask>(std::move(codec), buffer, dstInfo, padding);
  atlasCellCodecTasks.emplace_back(std::move(task));
}

void DrawingManager::clearAtlasCellCodecTasks() {
  atlasCellCodecTasks.clear();
  atlasCellDatas.clear();
}

void DrawingManager::uploadAtlasToGPU() {
  for (auto& task : atlasCellCodecTasks) {
    task->wait();
  }
  for (auto& [textureProxy, cellDatas] : atlasCellDatas) {
    if (textureProxy == nullptr || cellDatas.empty()) {
      continue;
    }
    auto texture = textureProxy->getTexture();
    if (texture == nullptr) {
      continue;
    }
    auto gpu = context->gpu();
    for (auto& [data, info, atlasOffset] : cellDatas) {
      if (data == nullptr) {
        continue;
      }
      auto rect = Rect::MakeXYWH(atlasOffset.x, atlasOffset.y, static_cast<float>(info.width()),
                                 static_cast<float>(info.height()));
      gpu->writePixels(texture->getSampler(), rect, data->data(), info.rowBytes());
    }
  }
  clearAtlasCellCodecTasks();
}

}  // namespace tgfx

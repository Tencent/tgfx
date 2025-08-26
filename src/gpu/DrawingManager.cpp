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
#include "GPU.h"
#include "ProxyProvider.h"
#include "core/AtlasCellDecodeTask.h"
#include "core/AtlasManager.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "gpu/proxies/TextureProxy.h"
#include "gpu/tasks/GenerateMipmapsTask.h"
#include "gpu/tasks/RenderTargetCopyTask.h"
#include "gpu/tasks/RuntimeDrawTask.h"
#include "gpu/tasks/SemaphoreWaitTask.h"
#include "inspect/InspectorMark.h"
#include "tgfx/core/RenderFlags.h"

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
  auto textureProxy = renderTarget->asTextureProxy();
  auto task = drawingBuffer->make<OpsRenderTask>(std::move(renderTarget), std::move(ops));
  renderTasks.emplace_back(std::move(task));
  addGenerateMipmapsTask(std::move(textureProxy));
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
  auto textureProxy = renderTarget->asTextureProxy();
  auto task = drawingBuffer->make<OpsRenderTask>(std::move(renderTarget), std::move(ops));
  renderTasks.emplace_back(std::move(task));
  addGenerateMipmapsTask(std::move(textureProxy));
}

void DrawingManager::addRuntimeDrawTask(std::shared_ptr<RenderTargetProxy> renderTarget,
                                        std::vector<std::shared_ptr<TextureProxy>> inputs,
                                        std::shared_ptr<RuntimeEffect> effect,
                                        const Point& offset) {
  if (renderTarget == nullptr || inputs.empty() || effect == nullptr) {
    return;
  }
  auto textureProxy = renderTarget->asTextureProxy();
  auto task = drawingBuffer->make<RuntimeDrawTask>(std::move(renderTarget), std::move(inputs),
                                                   std::move(effect), offset);
  renderTasks.emplace_back(std::move(task));
  addGenerateMipmapsTask(std::move(textureProxy));
}

void DrawingManager::addGenerateMipmapsTask(std::shared_ptr<TextureProxy> textureProxy) {
  if (textureProxy == nullptr || !textureProxy->hasMipmaps()) {
    return;
  }
  auto task = drawingBuffer->make<GenerateMipmapsTask>(std::move(textureProxy));
  renderTasks.emplace_back(std::move(task));
}

void DrawingManager::addRenderTargetCopyTask(std::shared_ptr<RenderTargetProxy> source,
                                             std::shared_ptr<TextureProxy> dest, int srcX,
                                             int srcY) {
  if (source == nullptr || dest == nullptr) {
    return;
  }
  auto task =
      drawingBuffer->make<RenderTargetCopyTask>(std::move(source), std::move(dest), srcX, srcY);
  renderTasks.emplace_back(std::move(task));
}

void DrawingManager::addResourceTask(PlacementPtr<ResourceTask> resourceTask) {
  if (resourceTask == nullptr) {
    return;
  }
  resourceTasks.emplace_back(std::move(resourceTask));
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

void DrawingManager::addSemaphoreWaitTask(std::shared_ptr<Semaphore> semaphore) {
  if (semaphore == nullptr) {
    return;
  }
  auto task = drawingBuffer->make<SemaphoreWaitTask>(std::move(semaphore));
  renderTasks.emplace_back(std::move(task));
}

std::shared_ptr<CommandBuffer> DrawingManager::flush(BackendSemaphore* signalSemaphore) {
  TASK_MARK(tgfx::inspect::OpTaskType::Flush);
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
    return nullptr;
  }
  {
    TASK_MARK(tgfx::inspect::OpTaskType::ResourceTask);
    for (auto& task : resourceTasks) {
      task->execute(context);
      task = nullptr;
    }
  }
  uploadAtlasToGPU();
  resourceTasks.clear();
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

  if (signalSemaphore != nullptr) {
    *signalSemaphore = commandEncoder->insertSemaphore();
  }
  return commandEncoder->finish();
}

void DrawingManager::releaseAll() {
  compositors.clear();
  resourceTasks.clear();
  renderTasks.clear();
  atlasCellCodecTasks.clear();
  atlasCellDatas.clear();
}

void DrawingManager::clearAtlasCellCodecTasks() {
  atlasCellCodecTasks.clear();
  atlasCellDatas.clear();
}

void DrawingManager::uploadAtlasToGPU() {
  auto queue = context->gpu()->queue();
  for (auto& task : atlasCellCodecTasks) {
    task->wait();
  }
  for (auto& [textureProxy, cellDatas] : atlasCellDatas) {
    if (textureProxy == nullptr || cellDatas.empty()) {
      continue;
    }
    auto textureView = textureProxy->getTextureView();
    if (textureView == nullptr) {
      continue;
    }
    for (auto& [data, info, atlasOffset] : cellDatas) {
      if (data == nullptr) {
        continue;
      }
      auto rect = Rect::MakeXYWH(atlasOffset.x, atlasOffset.y, static_cast<float>(info.width()),
                                 static_cast<float>(info.height()));
      queue->writeTexture(textureView->getTexture(), rect, data->data(), info.rowBytes());
      // Text atlas has no mipmaps, so we don't need to regenerate mipmaps.
    }
  }
  clearAtlasCellCodecTasks();
}

}  // namespace tgfx

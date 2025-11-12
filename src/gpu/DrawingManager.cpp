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
static ColorType GetAtlasColorType(bool isAlphaOnly) {
  if (isAlphaOnly) {
    return ColorType::ALPHA_8;
  }
#ifdef __APPLE__
  return ColorType::BGRA_8888;
#else
  return ColorType::RGBA_8888;
#endif
}

static ImageInfo GetAtlasImageInfo(int width, int height, bool isAlphaOnly) {
  auto colorType = GetAtlasColorType(isAlphaOnly);
  auto rowBytes = static_cast<size_t>(width * (isAlphaOnly ? 1 : 4));
  // Align to 4 bytes
  constexpr size_t ALIGNMENT = 4;
  rowBytes = (rowBytes + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
  return ImageInfo::Make(width, height, colorType, AlphaType::Premultiplied, rowBytes,
                         ColorSpace::MakeSRGB());
}

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
  auto task = drawingAllocator->make<OpsRenderTask>(std::move(renderTarget), std::move(drawOps),
                                                    std::nullopt);
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
                                      std::optional<Color> clearColor) {
  if (renderTarget == nullptr || (drawOps.empty() && !clearColor.has_value())) {
    return;
  }
  auto textureProxy = renderTarget->asTextureProxy();
  auto task = drawingAllocator->make<OpsRenderTask>(std::move(renderTarget), std::move(drawOps),
                                                    clearColor);
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
  auto task = drawingAllocator->make<RuntimeDrawTask>(std::move(renderTarget), std::move(inputs),
                                                      std::move(effect), offset);
  renderTasks.emplace_back(std::move(task));
  addGenerateMipmapsTask(std::move(textureProxy));
}

void DrawingManager::addGenerateMipmapsTask(std::shared_ptr<TextureProxy> textureProxy) {
  if (textureProxy == nullptr || !textureProxy->hasMipmaps()) {
    return;
  }
  auto task = drawingAllocator->make<GenerateMipmapsTask>(std::move(textureProxy));
  renderTasks.emplace_back(std::move(task));
}

void DrawingManager::addRenderTargetCopyTask(std::shared_ptr<RenderTargetProxy> source,
                                             std::shared_ptr<TextureProxy> dest, int srcX,
                                             int srcY) {
  if (source == nullptr || dest == nullptr) {
    return;
  }
  auto task =
      drawingAllocator->make<RenderTargetCopyTask>(std::move(source), std::move(dest), srcX, srcY);
  renderTasks.emplace_back(std::move(task));
}

void DrawingManager::addTransferPixelsTask(std::shared_ptr<RenderTargetProxy> source,
                                           const Rect& srcRect,
                                           std::shared_ptr<GPUBufferProxy> dest) {
  if (source == nullptr || dest == nullptr || srcRect.isEmpty()) {
    return;
  }
  auto task =
      drawingAllocator->make<TransferPixelsTask>(std::move(source), srcRect, std::move(dest));
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
  void* dstPixels = nullptr;
  auto dstWidth = codec->width() + 2 * padding;
  auto dstHeight = codec->height() + 2 * padding;
  ImageInfo dstInfo = {};
  auto hardwareBuffer = textureProxy->getHardwareBuffer();
  if (hardwareBuffer != nullptr) {
    auto hardwareInfo = GetImageInfo(hardwareBuffer, ColorSpace::MakeSRGB());
    dstInfo = hardwareInfo.makeIntersect(0, 0, dstWidth, dstHeight);
    void* pixels = nullptr;
    if (auto iter = atlasHardwareBuffers.find(textureProxy.get());
        iter == atlasHardwareBuffers.end()) {
      pixels = HardwareBufferLock(hardwareBuffer);
      atlasHardwareBuffers.emplace(textureProxy.get(), std::make_pair(hardwareBuffer, pixels));
    } else {
      pixels = iter->second.second;
    }
    auto offsetX = static_cast<int>(atlasOffset.x) - padding;
    auto offsetY = static_cast<int>(atlasOffset.y) - padding;
    dstPixels = hardwareInfo.computeOffset(pixels, offsetX, offsetY);
  } else {
    dstInfo = GetAtlasImageInfo(dstWidth, dstHeight, codec->isAlphaOnly());
    auto length = dstInfo.byteSize();
    dstPixels = drawingAllocator->allocate(length);
    if (dstPixels == nullptr) {
      return;
    }
    auto data = Data::MakeWithoutCopy(dstPixels, length);
    auto uploadOffset = atlasOffset;
    auto floatPadding = static_cast<float>(padding);
    uploadOffset.offset(-floatPadding, -floatPadding);
    atlasCellDatas[textureProxy].emplace_back(std::move(data), dstInfo, uploadOffset);
  }
  auto task = std::make_shared<AtlasCellDecodeTask>(std::move(codec), dstPixels, dstInfo, padding);
  atlasCellCodecTasks.emplace_back(std::move(task));
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

  if (resourceTasks.empty() && renderTasks.empty()) {
    proxyProvider->clearSharedVertexBuffer();
    resetAtlasCache();
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
  return commandEncoder->finish();
}

void DrawingManager::resetAtlasCache() {
  atlasCellCodecTasks.clear();
  atlasCellDatas.clear();
  atlasHardwareBuffers.clear();
}

void DrawingManager::uploadAtlasToGPU() {
  auto queue = context->gpu()->queue();
  for (auto& task : atlasCellCodecTasks) {
    task->wait();
  }
  for (auto& [_, buffer] : atlasHardwareBuffers) {
    HardwareBufferUnlock(buffer.first);
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
  resetAtlasCache();
}

}  // namespace tgfx

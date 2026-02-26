/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "AtlasUploadTask.h"
#include "core/AtlasTypes.h"
#include "core/utils/BlockAllocator.h"
#include "core/utils/ClearPixels.h"
#include "core/utils/HardwareBufferUtil.h"
#include "tgfx/core/Task.h"
#include "tgfx/gpu/GPU.h"

namespace tgfx {
class AsyncCellUploadTask : public Task, public CellUploadTask {
 public:
  AsyncCellUploadTask(std::shared_ptr<ImageCodec> imageCodec, void* dstPixels,
                      const ImageInfo& dstInfo, int offsetX, int offsetY, bool needsWriteTexture)
      : imageCodec(std::move(imageCodec)), dstPixels(dstPixels), dstInfo(dstInfo), offsetX(offsetX),
        offsetY(offsetY), needsWriteTexture(needsWriteTexture) {
  }

  void upload(std::shared_ptr<Texture> texture, CommandQueue* queue) override {
    wait();
    if (needsWriteTexture) {
      auto rect = Rect::MakeXYWH(offsetX, offsetY, dstInfo.width(), dstInfo.height());
      queue->writeTexture(std::move(texture), rect, dstPixels, dstInfo.rowBytes());
    }
  }

  ~AsyncCellUploadTask() override {
    Task::cancel();
  }

 protected:
  void onExecute() override {
    DEBUG_ASSERT(imageCodec != nullptr)
    ClearPixels(dstInfo, dstPixels);
    auto targetInfo = dstInfo.makeIntersect(0, 0, imageCodec->width(), imageCodec->height());
    auto targetPixels = dstInfo.computeOffset(dstPixels, Plot::CellPadding, Plot::CellPadding);
    imageCodec->readPixels(targetInfo, targetPixels);
    imageCodec = nullptr;
  }

  void onCancel() override {
    imageCodec = nullptr;
  }

 private:
  std::shared_ptr<ImageCodec> imageCodec = nullptr;
  void* dstPixels = nullptr;
  ImageInfo dstInfo = {};
  int offsetX = 0;
  int offsetY = 0;
  bool needsWriteTexture = false;
};

#ifndef TGFX_BUILD_FOR_WEB
PlacementPtr<AtlasUploadTask> AtlasUploadTask::Make(BlockAllocator* allocator,
                                                    std::shared_ptr<TextureProxy> proxy) {
  return allocator->make<AtlasUploadTask>(std::move(proxy));
}
#endif

AtlasUploadTask::AtlasUploadTask(std::shared_ptr<TextureProxy> proxy)
    : textureProxy(std::move(proxy)) {
  DEBUG_ASSERT(textureProxy != nullptr);
  auto hardwareBuffer = textureProxy->getHardwareBuffer();
  if (hardwareBuffer != nullptr) {
    hardwarePixels = HardwareBufferLock(hardwareBuffer);
    hardwareInfo = GetImageInfo(hardwareBuffer, ColorSpace::SRGB());
  }
}

AtlasUploadTask::~AtlasUploadTask() {
  if (hardwarePixels != nullptr) {
    auto hardwareBuffer = textureProxy->getHardwareBuffer();
    DEBUG_ASSERT(hardwareBuffer != nullptr);
    HardwareBufferUnlock(hardwareBuffer);
  }
}

static ImageInfo MakeAtlasCellInfo(int width, int height, bool isAlphaOnly) {
  auto colorType = ColorType::ALPHA_8;
  if (!isAlphaOnly) {
#ifdef __APPLE__
    colorType = ColorType::BGRA_8888;
#else
    colorType = ColorType::RGBA_8888;
#endif
  }
  auto rowBytes = static_cast<size_t>(width * (isAlphaOnly ? 1 : 4));
  // Align to 4 bytes
  constexpr size_t ALIGNMENT = 4;
  rowBytes = (rowBytes + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
  return ImageInfo::Make(width, height, colorType, AlphaType::Premultiplied, rowBytes,
                         ColorSpace::SRGB());
}

void AtlasUploadTask::addCell(BlockAllocator* allocator, std::shared_ptr<ImageCodec> codec,
                              const Point& atlasOffset) {
  DEBUG_ASSERT(codec != nullptr);
  auto padding = Plot::CellPadding;
  auto offsetX = static_cast<int>(atlasOffset.x) - padding;
  auto offsetY = static_cast<int>(atlasOffset.y) - padding;
  void* dstPixels = nullptr;
  auto dstWidth = codec->width() + 2 * padding;
  auto dstHeight = codec->height() + 2 * padding;
  ImageInfo dstInfo = {};
  if (hardwarePixels != nullptr) {
    dstInfo = hardwareInfo.makeIntersect(offsetX, offsetY, dstWidth, dstHeight);
    dstPixels = hardwareInfo.computeOffset(hardwarePixels, offsetX, offsetY);
  } else {
    dstInfo = MakeAtlasCellInfo(dstWidth, dstHeight, codec->isAlphaOnly());
    auto length = dstInfo.byteSize();
    dstPixels = allocator->allocate(length);
    if (dstPixels == nullptr) {
      LOGE("AtlasUploadTask::addCell failed to allocate %zu bytes for atlas cell", length);
      return;
    }
  }
  auto task = std::make_shared<AsyncCellUploadTask>(std::move(codec), dstPixels, dstInfo, offsetX,
                                                    offsetY, hardwarePixels == nullptr);
  Task::Run(task);
  cellTasks.emplace_back(std::move(task));
}

void AtlasUploadTask::upload(Context* context) {
  auto textureView = textureProxy->getTextureView();
  if (textureView == nullptr) {
    return;
  }
  auto texture = textureView->getTexture();
  auto queue = context->gpu()->queue();
  for (auto& task : cellTasks) {
    task->upload(texture, queue);
  }
  cellTasks.clear();
}

}  // namespace tgfx

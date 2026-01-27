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
#ifdef TGFX_BUILD_FOR_WEB
#include "platform/web/WebImageBuffer.h"
#endif

namespace tgfx {
class CellDecodeTask : public Task {
 public:
  CellDecodeTask(std::shared_ptr<ImageCodec> imageCodec, void* dstPixels, const ImageInfo& dstInfo,
                 int offsetX, int offsetY)
      : imageCodec(std::move(imageCodec)), dstPixels(dstPixels), dstInfo(dstInfo), offsetX(offsetX),
        offsetY(offsetY) {
  }

  bool asyncSupport() const {
    return imageCodec ? imageCodec->asyncSupport() : true;
  }

  const ImageInfo& info() const {
    return dstInfo;
  }

  void* pixels() const {
    return dstPixels;
  }

  Rect atlasRect() const {
    return Rect::MakeXYWH(offsetX, offsetY, dstInfo.width(), dstInfo.height());
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
};

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
  for (auto& task : tasks) {
    task->cancel();
  }
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
  // Check async support before creating task or moving codec.
  auto asyncSupport = codec->asyncSupport();

#ifdef TGFX_BUILD_FOR_WEB
  if (!asyncSupport && hardwarePixels == nullptr) {
    auto imageBuffer = codec->makeBuffer(false);
    if (imageBuffer != nullptr) {
      DirectUploadCell cell;
      cell.imageBuffer = std::move(imageBuffer);
      cell.offsetX = offsetX;
      cell.offsetY = offsetY;
      directUploadCells.push_back(std::move(cell));
      return;
    }
  }
#endif

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
  auto task =
      std::make_shared<CellDecodeTask>(std::move(codec), dstPixels, dstInfo, offsetX, offsetY);
  if (task->asyncSupport()) {
    Task::Run(task);
  } else {
    task->wait();
  }
  tasks.emplace_back(std::move(task));
}

void AtlasUploadTask::upload(Context* context) {
  auto textureView = textureProxy->getTextureView();
  if (textureView == nullptr) {
    return;
  }
#ifdef TGFX_BUILD_FOR_WEB
  for (auto& cell : directUploadCells) {
    auto webBuffer = std::static_pointer_cast<WebImageBuffer>(cell.imageBuffer);
    webBuffer->uploadToTexture(textureView->getTexture(), cell.offsetX, cell.offsetY);
  }
  directUploadCells.clear();
#endif
  auto queue = context->gpu()->queue();
  for (auto& task : tasks) {
    task->wait();
    if (!hardwarePixels) {
      queue->writeTexture(textureView->getTexture(), task->atlasRect(), task->pixels(),
                          task->info().rowBytes());
    }
  }
  tasks.clear();
}

}  // namespace tgfx

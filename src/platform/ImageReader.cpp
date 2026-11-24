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

#include "tgfx/platform/ImageReader.h"
#include "gpu/resources/TextureView.h"
#include "platform/ImageStream.h"

namespace tgfx {
class ImageReaderBuffer : public ImageBuffer {
 public:
  ImageReaderBuffer(std::shared_ptr<ImageReader> reader, uint64_t version)
      : reader(std::move(reader)), version(version) {
  }
  int width() const override {
    return reader->width();
  }
  int height() const override {
    return reader->height();
  }
  bool isAlphaOnly() const override {
    return false;
  }
  bool expired() const override {
    return delivery.load(std::memory_order_acquire) == ImageBufferState::Expired ||
           reader->checkExpired(version);
  }
  ImageBufferState state() const override {
    return expired() ? ImageBufferState::Expired : delivery.load(std::memory_order_acquire);
  }
  int64_t timestamp() const override {
    return deliveredTimestamp.load(std::memory_order_acquire);
  }
  const std::shared_ptr<ColorSpace>& colorSpace() const override {
    return reader->colorSpace();
  }

 protected:
  std::shared_ptr<TextureView> onMakeTexture(Context* context, bool mipmapped) const override {
    return reader->readTexture(version, context, mipmapped, delivery, deliveredTimestamp);
  }

 private:
  std::shared_ptr<ImageReader> reader;
  uint64_t version;
  mutable std::atomic<ImageBufferState> delivery = ImageBufferState::Pending;
  mutable std::atomic<int64_t> deliveredTimestamp = -1;
};

std::shared_ptr<ImageReader> ImageReader::MakeFrom(std::shared_ptr<ImageStream> stream) {
  if (stream == nullptr) {
    return nullptr;
  }
  auto reader = std::shared_ptr<ImageReader>(new ImageReader(std::move(stream)));
  reader->weakThis = reader;
  return reader;
}
ImageReader::ImageReader(std::shared_ptr<ImageStream> stream) : stream(std::move(stream)) {
}
int ImageReader::width() const {
  return stream->width();
}
int ImageReader::height() const {
  return stream->height();
}
const std::shared_ptr<ColorSpace>& ImageReader::colorSpace() const {
  return stream->colorSpace();
}
std::shared_ptr<ImageBuffer> ImageReader::acquireBuffer() {
  std::lock_guard<std::mutex> lock(locker);
  return std::make_shared<ImageReaderBuffer>(weakThis.lock(), ++bufferVersion);
}
std::shared_ptr<ImageBuffer> ImageReader::acquireNextBuffer() {
  return acquireBuffer();
}
bool ImageReader::checkExpired(uint64_t version) {
  std::lock_guard<std::mutex> lock(locker);
  return version != textureVersion && version < bufferVersion;
}
std::shared_ptr<TextureView> ImageReader::readTexture(uint64_t version, Context* context,
                                                      bool mipmapped,
                                                      std::atomic<ImageBufferState>& state,
                                                      std::atomic<int64_t>& timestamp) {
  std::lock_guard<std::mutex> lock(locker);
  if (state.load() == ImageBufferState::Expired) {
    return nullptr;
  }
  if (version == textureVersion) {
    return textureView;
  }
  if (version < bufferVersion) {
    state.store(ImageBufferState::Expired, std::memory_order_release);
    return nullptr;
  }
  // Bound one read when a producer continuously queues images and timestamps never stop advancing.
  // This limits consumption attempts, not driver/GPU execution time or the distance to a target.
  constexpr int MAX_CONSUMPTIONS = 8;
  bool delivered = false;
  int64_t deliveredTimestamp = -1;
  for (int count = 0; count < MAX_CONSUMPTIONS; count++) {
    auto result = stream->onReadTexture(context, mipmapped, textureView);
    if (result.advanced || result.contentChanged || result.texture != textureView) {
      textureVersion = 0;
      if (textureView) {
        textureView->removeUniqueKey();
      }
      if (result.texture && result.texture != textureView) {
        result.texture->removeUniqueKey();
      }
    }
    textureView = std::move(result.texture);
    if (textureView == nullptr || result.failed) {
      delivered = false;
      break;
    }
    if (!result.advanced) {
      break;
    }
    delivered = true;
    deliveredTimestamp = result.timestamp;
    if (!result.canContinue) {
      break;
    }
  }
  if (!delivered || textureView == nullptr) {
    state.store(ImageBufferState::Expired, std::memory_order_release);
    return nullptr;
  }
  textureVersion = version;
  timestamp.store(deliveredTimestamp, std::memory_order_relaxed);
  state.store(ImageBufferState::Ready, std::memory_order_release);
  return textureView;
}
}  // namespace tgfx

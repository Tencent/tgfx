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
#include "core/utils/Log.h"
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
    std::lock_guard<std::mutex> lock(reader->locker);
    return isExpired();
  }
  ImageBufferState state() const override {
    std::lock_guard<std::mutex> lock(reader->locker);
    return isExpired() ? ImageBufferState::Expired : delivery;
  }
  int64_t timestamp() const override {
    std::lock_guard<std::mutex> lock(reader->locker);
    return deliveredTimestamp;
  }
  const std::shared_ptr<ColorSpace>& colorSpace() const override {
    return reader->colorSpace();
  }

 protected:
  std::shared_ptr<TextureView> onMakeTexture(Context* context, bool mipmapped) const override {
    std::lock_guard<std::mutex> lock(reader->locker);
    if (isExpired()) {
      return nullptr;
    }
    auto result = reader->readTexture(version, context, mipmapped);
    if (!result.delivered) {
      delivery = ImageBufferState::Expired;
      return nullptr;
    }
    deliveredTimestamp = result.timestamp;
    delivery = ImageBufferState::Ready;
    return result.texture;
  }

 private:
  // Requires reader->locker. Do not call the locking public expired() while holding this mutex.
  bool isExpired() const {
    return delivery == ImageBufferState::Expired || reader->checkExpired(version);
  }
  std::shared_ptr<ImageReader> reader;
  uint64_t version;
  mutable ImageBufferState delivery = ImageBufferState::Pending;
  mutable int64_t deliveredTimestamp = -1;
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
  auto reader = weakThis.lock();
  DEBUG_ASSERT(reader != nullptr);
  return std::make_shared<ImageReaderBuffer>(std::move(reader), ++bufferVersion);
}

std::shared_ptr<ImageBuffer> ImageReader::acquireNextBuffer() {
  return acquireBuffer();
}

bool ImageReader::checkExpired(uint64_t version) {
  return version != textureVersion && version < bufferVersion;
}

ImageReader::ReadResult ImageReader::readTexture(uint64_t version, Context* context,
                                                 bool mipmapped) {
  if (version == textureVersion) {
    return {textureView, true, textureTimestamp};
  }
  if (version < bufferVersion) {
    return {};
  }
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
  if (textureView == nullptr || result.failed || !result.advanced) {
    return {};
  }
  textureVersion = version;
  textureTimestamp = result.timestamp;
  return {textureView, true, result.timestamp};
}
}  // namespace tgfx

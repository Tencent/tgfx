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
  ImageReaderBuffer(std::shared_ptr<ImageReader> imageReader, uint64_t bufferVersion,
                    std::shared_ptr<ImageReaderFrame> frame)
      : imageReader(std::move(imageReader)), contentVersion(bufferVersion),
        frame(std::move(frame)) {
  }

  int width() const override {
    return imageReader->width();
  }

  int height() const override {
    return imageReader->height();
  }

  bool isAlphaOnly() const override {
    return false;
  }

  bool expired() const override {
    return imageReader->checkExpired(contentVersion);
  }

  const std::shared_ptr<ColorSpace>& colorSpace() const override {
    return imageReader->colorSpace();
  }

 protected:
  std::shared_ptr<TextureView> onMakeTexture(Context* context, bool mipmapped) const override {
    return imageReader->readTexture(contentVersion, context, mipmapped, frame);
  }

 private:
  std::shared_ptr<ImageReader> imageReader = nullptr;
  uint64_t contentVersion = 0;
  std::shared_ptr<ImageReaderFrame> frame = nullptr;
};

std::shared_ptr<ImageReader> ImageReader::MakeFrom(std::shared_ptr<ImageStream> imageStream) {
  if (imageStream == nullptr) {
    return nullptr;
  }
  auto imageReader = std::shared_ptr<ImageReader>(new ImageReader(std::move(imageStream)));
  imageReader->weakThis = imageReader;
  return imageReader;
}

ImageReader::ImageReader(std::shared_ptr<ImageStream> imageStream)
    : stream(std::move(imageStream)) {
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

std::shared_ptr<ImageBuffer> ImageReader::acquireNextBuffer(
    std::shared_ptr<ImageReaderFrame> frame) {
  std::lock_guard<std::mutex> autoLock(locker);
  DEBUG_ASSERT(!weakThis.expired());
  bufferVersion++;
  return std::make_shared<ImageReaderBuffer>(weakThis.lock(), bufferVersion, std::move(frame));
}

void ImageReader::notifySeeked() {
  std::lock_guard<std::mutex> autoLock(locker);
  bufferVersion++;
  textureVersion = 0;
  if (stream != nullptr) {
    stream->onSeeked();
  }
}

bool ImageReader::checkExpired(uint64_t contentVersion) {
  std::lock_guard<std::mutex> autoLock(locker);
  return contentVersion != textureVersion && contentVersion < bufferVersion;
}

std::shared_ptr<TextureView> ImageReader::readTexture(
    uint64_t contentVersion, Context* context, bool mipmapped,
    const std::shared_ptr<ImageReaderFrame>& frame) {
  std::lock_guard<std::mutex> autoLock(locker);
  if (contentVersion == textureVersion) {
    return textureView;
  }
  if (contentVersion < bufferVersion) {
    if (frame != nullptr) {
      frame->status.store(ImageReaderStatus::Expired, std::memory_order_release);
    }
    return nullptr;
  }
  auto target = frame != nullptr ? frame->targetTimestamp : -1;
  auto deadline = frame != nullptr ? frame->deadline.load(std::memory_order_acquire) : 0;
  auto result = stream->onReadTexture(context, mipmapped, textureView, target, deadline);
  if (result.status == ImageReaderStatus::Ready && target >= 0 && result.timestamp != target) {
    result.status = ImageReaderStatus::Mismatched;
  }
  if (result.status == ImageReaderStatus::Ready && result.texture == nullptr) {
    result.status = ImageReaderStatus::Failed;
  }
  textureView = std::move(result.texture);
  textureVersion = 0;
  if (textureView != nullptr) {
    textureView->removeUniqueKey();
    if (result.status == ImageReaderStatus::Ready ||
        result.status == ImageReaderStatus::Unsupported) {
      textureVersion = contentVersion;
    }
  }
  if (frame != nullptr) {
    frame->timestamp.store(result.timestamp, std::memory_order_relaxed);
    frame->status.store(result.status, std::memory_order_release);
  }
  if (result.status != ImageReaderStatus::Ready &&
      result.status != ImageReaderStatus::Unsupported) {
    return nullptr;
  }
  return textureView;
}
}  // namespace tgfx

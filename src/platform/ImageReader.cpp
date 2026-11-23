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
                    int64_t target, std::shared_ptr<ImageReaderOptions> options,
                    std::shared_ptr<ImageReaderResult> receipt)
      : imageReader(std::move(imageReader)), contentVersion(bufferVersion), target(target),
        options(std::move(options)), receipt(std::move(receipt)) {
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
    return imageReader->readTexture(contentVersion, context, mipmapped, target, options, receipt);
  }

 private:
  std::shared_ptr<ImageReader> imageReader = nullptr;
  uint64_t contentVersion = 0;
  int64_t target = -1;
  std::shared_ptr<ImageReaderOptions> options = nullptr;
  std::shared_ptr<ImageReaderResult> receipt = nullptr;
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

std::shared_ptr<ImageBuffer> ImageReader::acquireBuffer(std::shared_ptr<ImageReaderOptions> options,
                                                        std::shared_ptr<ImageReaderResult> result) {
  std::lock_guard<std::mutex> autoLock(locker);
  DEBUG_ASSERT(!weakThis.expired());
  bufferVersion++;
  auto target = options != nullptr ? options->targetTimestamp : -1;
  return std::make_shared<ImageReaderBuffer>(weakThis.lock(), bufferVersion, target,
                                             std::move(options), std::move(result));
}

std::shared_ptr<ImageBuffer> ImageReader::acquireNextBuffer() {
  return acquireBuffer();
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
    uint64_t contentVersion, Context* context, bool mipmapped, int64_t target,
    const std::shared_ptr<ImageReaderOptions>& options,
    const std::shared_ptr<ImageReaderResult>& receipt) {
  std::lock_guard<std::mutex> autoLock(locker);
  if (contentVersion == textureVersion) {
    return textureView;
  }
  if (contentVersion < bufferVersion) {
    if (receipt != nullptr) {
      receipt->status.store(ImageReaderStatus::Expired, std::memory_order_release);
    }
    return nullptr;
  }
  auto deadline = options != nullptr ? options->deadline.load(std::memory_order_acquire) : 0;
  auto result = stream->onReadTexture(context, mipmapped, textureView, target, deadline);
  if (result.status == ImageReaderStatus::Ready && target >= 0 && result.timestamp != target) {
    result.status = ImageReaderStatus::Mismatched;
  }
  if (result.status == ImageReaderStatus::Ready && result.texture == nullptr) {
    result.status = ImageReaderStatus::Failed;
  }
  if (result.contentChanged || result.texture != textureView) {
    textureVersion = 0;
    if (textureView != nullptr) {
      textureView->removeUniqueKey();
    }
    if (result.texture != nullptr) {
      result.texture->removeUniqueKey();
    }
  }
  textureView = std::move(result.texture);
  if (textureView != nullptr) {
    if (result.status == ImageReaderStatus::Ready ||
        result.status == ImageReaderStatus::Unsupported) {
      textureVersion = contentVersion;
    }
  }
  if (receipt != nullptr) {
    receipt->timestamp.store(result.timestamp, std::memory_order_relaxed);
    receipt->status.store(result.status, std::memory_order_release);
  }
  if (result.status != ImageReaderStatus::Ready &&
      result.status != ImageReaderStatus::Unsupported) {
    return nullptr;
  }
  return textureView;
}
}  // namespace tgfx

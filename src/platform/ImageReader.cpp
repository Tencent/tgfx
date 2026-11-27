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
                    int64_t timestamp)
      : imageReader(std::move(imageReader)), contentVersion(bufferVersion), timestamp(timestamp) {
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

  ImageBufferStatus status() const override {
    return imageReader->getStatus(contentVersion);
  }

  const std::shared_ptr<ColorSpace>& colorSpace() const override {
    return imageReader->colorSpace();
  }

 protected:
  std::shared_ptr<TextureView> onMakeTexture(Context* context, bool mipmapped) const override {
    return imageReader->readTexture(contentVersion, timestamp, context, mipmapped);
  }

 private:
  std::shared_ptr<ImageReader> imageReader = nullptr;
  uint64_t contentVersion = 0;
  int64_t timestamp = -1;
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

std::shared_ptr<ImageBuffer> ImageReader::acquireNextBuffer(int64_t timestamp) {
  std::lock_guard<std::mutex> autoLock(locker);
  DEBUG_ASSERT(!weakThis.expired());
  bufferVersion++;
  return std::make_shared<ImageReaderBuffer>(weakThis.lock(), bufferVersion, timestamp);
}

bool ImageReader::checkExpired(uint64_t contentVersion) {
  std::lock_guard<std::mutex> autoLock(locker);
  return contentVersion != textureVersion && contentVersion < bufferVersion;
}

ImageBufferStatus ImageReader::getStatus(uint64_t contentVersion) {
  std::lock_guard<std::mutex> autoLock(locker);
  if (contentVersion == textureVersion) {
    return ImageBufferStatus::Ready;
  }
  if (contentVersion < bufferVersion) {
    return ImageBufferStatus::Expired;
  }
  if (contentVersion == failedVersion) {
    return failedStatus;
  }
  return ImageBufferStatus::Pending;
}

std::shared_ptr<TextureView> ImageReader::readTexture(uint64_t contentVersion, int64_t timestamp,
                                                      Context* context, bool mipmapped) {
  std::lock_guard<std::mutex> autoLock(locker);
  if (contentVersion == textureVersion && contentValid) {
    return textureView;
  }
  if (contentVersion < bufferVersion) {
    LOGE(
        "ImageReader::readTexture(): Failed to read the texture view, the target ImageBuffer is "
        "already expired!");
    return nullptr;
  }
  auto result = stream->onReadTexture(context, mipmapped, textureView, timestamp);
  auto changed = result.contentChanged || result.textureView != textureView;
  if (changed) {
    if (textureView != nullptr) {
      textureView->removeUniqueKey();
    }
    if (result.textureView != nullptr && result.textureView != textureView) {
      result.textureView->removeUniqueKey();
    }
    textureVersion = 0;
    contentValid = false;
  }
  textureView = std::move(result.textureView);
  if (textureView != nullptr &&
      (result.status == ImageBufferStatus::Ready ||
       (result.status == ImageBufferStatus::Timeout && result.contentChanged))) {
    contentValid = true;
  }
  if (result.status == ImageBufferStatus::Ready && contentValid) {
    textureVersion = contentVersion;
  } else {
    failedVersion = contentVersion;
    failedStatus =
        result.status == ImageBufferStatus::Ready ? ImageBufferStatus::Failed : result.status;
  }
  // A timeout can retain successfully latched non-target pixels. Unknown failed mutations cannot.
  return contentValid ? textureView : nullptr;
}
}  // namespace tgfx

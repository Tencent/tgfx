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
#include "core/PixelRef.h"
#include "../../include/tgfx/core/Log.h"
#include "gpu/resources/TextureView.h"
#include "platform/ImageStream.h"

namespace tgfx {
class ImageReaderBuffer : public ImageBuffer {
 public:
  explicit ImageReaderBuffer(std::shared_ptr<ImageReader> imageReader, uint64_t bufferVersion)
      : imageReader(std::move(imageReader)), contentVersion(bufferVersion) {
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

  std::shared_ptr<ColorSpace> colorSpace() const override {
    return imageReader->colorSpace();
  }

 protected:
  std::shared_ptr<TextureView> onMakeTexture(Context* context, bool mipmapped) const override {
    return imageReader->readTexture(contentVersion, context, mipmapped);
  }

 private:
  std::shared_ptr<ImageReader> imageReader = nullptr;
  uint64_t contentVersion = 0;
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

std::shared_ptr<ColorSpace> ImageReader::colorSpace() const {
  return stream->colorSpace();
}

std::shared_ptr<ImageBuffer> ImageReader::acquireNextBuffer() {
  std::lock_guard<std::mutex> autoLock(locker);
  DEBUG_ASSERT(!weakThis.expired());
  bufferVersion++;
  return std::make_shared<ImageReaderBuffer>(weakThis.lock(), bufferVersion);
}

bool ImageReader::checkExpired(uint64_t contentVersion) {
  std::lock_guard<std::mutex> autoLock(locker);
  return contentVersion != textureVersion && contentVersion < bufferVersion;
}

std::shared_ptr<TextureView> ImageReader::readTexture(uint64_t contentVersion, Context* context,
                                                      bool mipmapped) {
  std::lock_guard<std::mutex> autoLock(locker);
  if (contentVersion == textureVersion) {
    return textureView;
  }
  if (contentVersion < bufferVersion) {
    LOGE(
        "ImageReader::readTexture(): Failed to read the texture view, the target ImageBuffer is "
        "already expired!");
    return nullptr;
  }
  bool success = true;
  if (textureView == nullptr) {
    textureView = stream->onMakeTexture(context, mipmapped);
    success = textureView != nullptr;
  } else {
    success = stream->onUpdateTexture(textureView);
  }
  if (success) {
    textureView->removeUniqueKey();
    textureVersion = contentVersion;
  }
  return textureView;
}
}  // namespace tgfx
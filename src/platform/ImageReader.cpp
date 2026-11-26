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
  ImageReaderBuffer(std::shared_ptr<ImageReader> reader,
                    std::shared_ptr<ImageReader::Credential> credential)
      : reader(std::move(reader)), credential(std::move(credential)) {
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
    return reader->checkExpired(*credential);
  }

  ImageBufferState state() const override {
    std::lock_guard<std::mutex> lock(reader->locker);
    return reader->checkExpired(*credential) ? ImageBufferState::Expired : credential->delivery;
  }

  int64_t timestamp() const override {
    std::lock_guard<std::mutex> lock(reader->locker);
    return credential->timestamp;
  }

  const std::shared_ptr<ColorSpace>& colorSpace() const override {
    return reader->colorSpace();
  }

 protected:
  std::shared_ptr<TextureView> onMakeTexture(Context* context, bool mipmapped) const override {
    std::lock_guard<std::mutex> lock(reader->locker);
    if (reader->checkExpired(*credential)) {
      return nullptr;
    }
    return reader->readTexture(*credential, context, mipmapped);
  }

 private:
  const std::shared_ptr<ImageReader> reader;
  const std::shared_ptr<ImageReader::Credential> credential;
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
  auto credential = std::make_shared<Credential>();
  credential->version = ++bufferVersion;
  return std::make_shared<ImageReaderBuffer>(std::move(reader), std::move(credential));
}

std::shared_ptr<ImageBuffer> ImageReader::acquireNextBuffer() {
  return acquireBuffer();
}

bool ImageReader::checkExpired(const Credential& credential) const {
  if (credential.delivery == ImageBufferState::Expired) {
    return true;
  }
  if (credential.delivery == ImageBufferState::Ready) {
    return !contentValid || credential.contentVersion != contentVersion;
  }
  return credential.version < bufferVersion;
}

std::shared_ptr<TextureView> ImageReader::readTexture(Credential& credential, Context* context,
                                                      bool mipmapped) {
  DEBUG_ASSERT(!checkExpired(credential));
  if (credential.delivery == ImageBufferState::Ready) {
    return textureView;
  }
  auto result = stream->onReadTexture(context, mipmapped, textureView);
  auto changed =
      result.outcome != ImageStream::ReadOutcome::Unchanged || result.texture != textureView;
  if (changed) {
    ++contentVersion;
    contentValid = false;
    textureTimestamp = -1;
    if (textureView != nullptr) {
      textureView->removeUniqueKey();
    }
    if (result.texture != nullptr && result.texture != textureView) {
      result.texture->removeUniqueKey();
    }
  }
  textureView = std::move(result.texture);
  if (result.outcome == ImageStream::ReadOutcome::Delivered && textureView != nullptr) {
    contentValid = true;
    textureTimestamp = result.timestamp;
  }
  if (!contentValid || textureView == nullptr) {
    credential.delivery = ImageBufferState::Expired;
    return nullptr;
  }
  credential.delivery = ImageBufferState::Ready;
  credential.contentVersion = contentVersion;
  credential.timestamp = textureTimestamp;
  return textureView;
}
}  // namespace tgfx

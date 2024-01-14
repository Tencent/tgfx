/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "TextureCreateTask.h"
#include "gpu/Texture.h"

namespace tgfx {
class EmptyTextureTask : public TextureCreateTask {
 public:
  EmptyTextureTask(ResourceKey strongKey, int width, int height, PixelFormat format, bool mipMapped,
                   ImageOrigin origin)
      : TextureCreateTask(std::move(strongKey)), width(width), height(height), format(format),
        mipMapped(mipMapped), origin(origin) {
  }

  std::shared_ptr<Resource> onMakeResource(Context* context) override {
    auto texture = Texture::MakeFormat(context, width, height, format, mipMapped, origin);
    if (texture == nullptr) {
      LOGE("EmptyTextureTask::onMakeResource() Failed to create the texture!");
    }
    return texture;
  }

 private:
  int width = 0;
  int height = 0;
  PixelFormat format = PixelFormat::RGBA_8888;
  bool mipMapped = false;
  ImageOrigin origin = ImageOrigin::TopLeft;
};

std::shared_ptr<TextureCreateTask> TextureCreateTask::MakeFrom(ResourceKey strongKey, int width,
                                                               int height, PixelFormat format,
                                                               bool mipMapped, ImageOrigin origin) {
  if (width <= 0 || height <= 0) {
    return nullptr;
  }
  return std::shared_ptr<TextureCreateTask>(
      new EmptyTextureTask(std::move(strongKey), width, height, format, mipMapped, origin));
}

class ImageDecoderTask : public TextureCreateTask {
 public:
  ImageDecoderTask(ResourceKey strongKey, std::shared_ptr<ImageDecoder> decoder, bool mipMapped)
      : TextureCreateTask(std::move(strongKey)), decoder(std::move(decoder)), mipMapped(mipMapped) {
  }

  std::shared_ptr<Resource> onMakeResource(Context* context) override {
    if (decoder == nullptr) {
      return nullptr;
    }
    auto imageBuffer = decoder->decode();
    if (imageBuffer == nullptr) {
      LOGE("ImageDecoderTask::onMakeResource() Failed to decode the image!");
      return nullptr;
    }
    auto texture = Texture::MakeFrom(context, imageBuffer, mipMapped);
    if (texture == nullptr) {
      LOGE("ImageDecoderTask::onMakeResource() Failed to create the texture!");
    } else {
      // Free the decoded image buffer immediately to reduce memory pressure.
      decoder = nullptr;
    }
    return texture;
  }

 private:
  std::shared_ptr<ImageDecoder> decoder = nullptr;
  bool mipMapped = false;
};

std::shared_ptr<TextureCreateTask> TextureCreateTask::MakeFrom(
    ResourceKey strongKey, std::shared_ptr<ImageDecoder> decoder, bool mipMapped) {
  if (decoder == nullptr) {
    return nullptr;
  }
  return std::make_shared<ImageDecoderTask>(std::move(strongKey), std::move(decoder), mipMapped);
}
}  // namespace tgfx

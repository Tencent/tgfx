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

#include "PlainTexture.h"
#include "core/utils/PixelFormatUtil.h"
#include "core/utils/UniqueID.h"
#include "gpu/Gpu.h"

namespace tgfx {
static ScratchKey ComputeScratchKey(int width, int height, PixelFormat format, bool mipmapped) {
  static const uint32_t PlainTextureType = UniqueID::Next();
  BytesKey bytesKey(4);
  bytesKey.write(PlainTextureType);
  bytesKey.write(width);
  bytesKey.write(height);
  auto formatValue = static_cast<uint32_t>(format);
  auto mipmapValue = static_cast<uint32_t>(mipmapped ? 1 : 0);
  bytesKey.write(formatValue | (mipmapValue << 30));
  return bytesKey;
}

std::shared_ptr<Texture> Texture::MakeFormat(Context* context, int width, int height,
                                             const void* pixels, size_t rowBytes,
                                             PixelFormat pixelFormat, bool mipmapped,
                                             ImageOrigin origin) {
  if (!PlainTexture::CheckSizeAndFormat(context, width, height, pixelFormat)) {
    return nullptr;
  }
  auto caps = context->caps();
  int maxMipmapLevel = mipmapped ? caps->getMaxMipmapLevel(width, height) : 0;
  auto scratchKey = ComputeScratchKey(width, height, pixelFormat, maxMipmapLevel > 0);
  auto texture = Resource::Find<Texture>(context, scratchKey);
  if (texture) {
    texture->_origin = origin;
  } else {
    auto sampler = context->gpu()->createSampler(width, height, pixelFormat, maxMipmapLevel + 1);
    if (sampler == nullptr) {
      return nullptr;
    }
    auto plainTexture = new PlainTexture(std::move(sampler), width, height, origin);
    texture = Resource::AddToCache(context, plainTexture, scratchKey);
  }
  if (pixels != nullptr) {
    context->gpu()->writePixels(texture->getSampler(), Rect::MakeWH(width, height), pixels,
                                rowBytes);
  }
  return texture;
}

bool PlainTexture::CheckSizeAndFormat(Context* context, int width, int height, PixelFormat format) {
  if (context == nullptr || width < 1 || height < 1) {
    return false;
  }
  if (format != PixelFormat::ALPHA_8 && format != PixelFormat::RGBA_8888 &&
      format != PixelFormat::BGRA_8888) {
    return false;
  }
  auto caps = context->caps();
  auto maxTextureSize = caps->maxTextureSize;
  return width <= maxTextureSize && height <= maxTextureSize;
}

PlainTexture::PlainTexture(std::unique_ptr<TextureSampler> sampler, int width, int height,
                           ImageOrigin origin)
    : Texture(width, height, origin), sampler(std::move(sampler)) {
}

size_t PlainTexture::memoryUsage() const {
  auto colorSize = static_cast<size_t>(width()) * static_cast<size_t>(height()) *
                   PixelFormatBytesPerPixel(sampler->format);
  return sampler->hasMipmaps() ? colorSize * 4 / 3 : colorSize;
}

void PlainTexture::onReleaseGPU() {
  context->gpu()->deleteSampler(sampler.get());
}
}  // namespace tgfx

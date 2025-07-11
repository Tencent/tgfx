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

#include "gpu/Texture.h"
#include "core/utils/PixelFormatUtil.h"
#include "core/utils/UniqueID.h"
#include "gpu/DefaultTexture.h"
#include "gpu/Gpu.h"
#include "gpu/YUVTexture.h"
#if defined(__OHOS__)
#include <native_buffer/native_buffer.h>
#endif

namespace tgfx {
bool Texture::CheckSizeAndFormat(Context* context, int width, int height, PixelFormat format) {
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

static ScratchKey ComputeTextureScratchKey(int width, int height, PixelFormat format,
                                           bool mipmapped) {
  static const uint32_t DefaultTextureType = UniqueID::Next();
  BytesKey bytesKey(4);
  bytesKey.write(DefaultTextureType);
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
  if (!CheckSizeAndFormat(context, width, height, pixelFormat)) {
    return nullptr;
  }
  auto hasMipmaps = context->caps()->mipmapSupport ? mipmapped : false;
  auto scratchKey = ComputeTextureScratchKey(width, height, pixelFormat, hasMipmaps);
  auto texture = Resource::Find<Texture>(context, scratchKey);
  if (texture) {
    texture->_origin = origin;
  } else {
    auto sampler = TextureSampler::Make(context, width, height, pixelFormat, hasMipmaps);
    if (sampler == nullptr) {
      return nullptr;
    }
    texture = Resource::AddToCache(
        context, new DefaultTexture(std::move(sampler), width, height, origin), scratchKey);
  }
  if (pixels != nullptr) {
    auto sampler = texture->getSampler();
    sampler->writePixels(context, Rect::MakeWH(width, height), pixels, rowBytes);
    sampler->regenerateMipmapLevels(context);
  }
  return texture;
}

std::shared_ptr<Texture> Texture::MakeFrom(Context* context, const BackendTexture& backendTexture,
                                           ImageOrigin origin, bool adopted) {
  if (context == nullptr || !backendTexture.isValid()) {
    return nullptr;
  }
  auto sampler = TextureSampler::MakeFrom(context, backendTexture, adopted);
  if (sampler == nullptr) {
    return nullptr;
  }
  ScratchKey scratchKey = {};
  if (adopted) {
    scratchKey = ComputeTextureScratchKey(backendTexture.width(), backendTexture.height(),
                                          sampler->format(), sampler->hasMipmaps());
  }
  auto texture = new DefaultTexture(std::move(sampler), backendTexture.width(),
                                    backendTexture.height(), origin);
  return Resource::AddToCache(context, texture, scratchKey);
}

std::shared_ptr<Texture> Texture::MakeFrom(Context* context,
                                           std::shared_ptr<ImageBuffer> imageBuffer,
                                           bool mipmapped) {
  if (context == nullptr || imageBuffer == nullptr) {
    return nullptr;
  }
  return imageBuffer->onMakeTexture(context, mipmapped);
}

std::shared_ptr<Texture> Texture::MakeFrom(Context* context, HardwareBufferRef hardwareBuffer,
                                           YUVColorSpace colorSpace) {
  auto size = HardwareBufferGetSize(hardwareBuffer);
  if (size.isEmpty()) {
    return nullptr;
  }

#if defined(__OHOS__)
  // On the HarmonyOS platform, video-decoded HardwareBuffers may not have the correct color space
  // set, so we need to set it manually before creating the texture.
  OH_NativeBuffer_Config config;
  OH_NativeBuffer_GetConfig(hardwareBuffer, &config);
  if (config.format >= NATIVEBUFFER_PIXEL_FMT_YUV_422_I &&
      config.format <= NATIVEBUFFER_PIXEL_FMT_YCRCB_P010) {
    switch (colorSpace) {
      case YUVColorSpace::BT601_LIMITED:
        OH_NativeBuffer_SetColorSpace(hardwareBuffer, OH_COLORSPACE_BT601_SMPTE_C_LIMIT);
        break;
      case YUVColorSpace::BT601_FULL:
        OH_NativeBuffer_SetColorSpace(hardwareBuffer, OH_COLORSPACE_BT601_SMPTE_C_FULL);
        break;
      case YUVColorSpace::BT709_LIMITED:
        OH_NativeBuffer_SetColorSpace(hardwareBuffer, OH_COLORSPACE_BT709_LIMIT);
        break;
      case YUVColorSpace::BT709_FULL:
        OH_NativeBuffer_SetColorSpace(hardwareBuffer, OH_COLORSPACE_BT709_FULL);
        break;
      case YUVColorSpace::BT2020_LIMITED:
        OH_NativeBuffer_SetColorSpace(hardwareBuffer, OH_COLORSPACE_BT2020_PQ_LIMIT);
        break;
      case YUVColorSpace::BT2020_FULL:
        OH_NativeBuffer_SetColorSpace(hardwareBuffer, OH_COLORSPACE_BT2020_PQ_FULL);
        break;
      default:
        break;
    }
  }
#endif

  YUVFormat yuvFormat = YUVFormat::Unknown;
  auto samplers = TextureSampler::MakeFrom(context, hardwareBuffer, &yuvFormat);
  if (samplers.empty()) {
    return nullptr;
  }
  Texture* texture = nullptr;
  if (samplers.size() == 1) {
    texture = new DefaultTexture(std::move(samplers.front()), size.width, size.height);
  } else {
    texture = new YUVTexture(std::move(samplers), size.width, size.height, yuvFormat, colorSpace);
  }
  return Resource::AddToCache(context, texture);
}

Point Texture::getTextureCoord(float x, float y) const {
  if (getSampler()->type() == SamplerType::Rectangle) {
    return {x, y};
  }
  return {x / static_cast<float>(width()), y / static_cast<float>(height())};
}
}  // namespace tgfx

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

#include "TextureView.h"
#include "core/utils/PixelFormatUtil.h"
#include "core/utils/UniqueID.h"
#include "gpu/GPU.h"
#include "gpu/resources/DefaultTextureView.h"
#include "gpu/resources/YUVTextureView.h"
#if defined(__OHOS__)
#include <native_buffer/native_buffer.h>
#endif

namespace tgfx {
bool TextureView::CheckSizeAndFormat(Context* context, int width, int height, PixelFormat format) {
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

void TextureView::ComputeTextureKey(const std::shared_ptr<GPUTexture> texture, BytesKey* bytesKey) {
  DEBUG_ASSERT(texture != nullptr);
  bytesKey->write(static_cast<uint32_t>(texture->format()) << 16 |
                  static_cast<uint32_t>(texture->type()));
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

std::shared_ptr<TextureView> TextureView::MakeFormat(Context* context, int width, int height,
                                                     const void* pixels, size_t rowBytes,
                                                     PixelFormat pixelFormat, bool mipmapped,
                                                     ImageOrigin origin) {
  if (!CheckSizeAndFormat(context, width, height, pixelFormat)) {
    return nullptr;
  }
  auto gpu = context->gpu();
  auto scratchKey = ComputeTextureScratchKey(width, height, pixelFormat, mipmapped);
  auto textureView = Resource::Find<TextureView>(context, scratchKey);
  if (textureView) {
    textureView->_origin = origin;
  } else {
    GPUTextureDescriptor descriptor = {width, height, pixelFormat, mipmapped};
    auto texture = gpu->createTexture(descriptor);
    if (texture == nullptr) {
      return nullptr;
    }
    textureView = Resource::AddToCache(context, new DefaultTextureView(std::move(texture), origin),
                                       scratchKey);
  }
  if (pixels != nullptr) {
    auto texture = textureView->getTexture();
    gpu->queue()->writeTexture(texture, Rect::MakeWH(width, height), pixels, rowBytes);
  }
  return textureView;
}

std::shared_ptr<TextureView> TextureView::MakeFrom(Context* context,
                                                   const BackendTexture& backendTexture,
                                                   ImageOrigin origin, bool adopted) {
  if (context == nullptr) {
    return nullptr;
  }
  auto texture = context->gpu()->importExternalTexture(backendTexture,
                                                       GPUTextureUsage::TEXTURE_BINDING, adopted);
  if (texture == nullptr) {
    return nullptr;
  }
  ScratchKey scratchKey = {};
  if (adopted) {
    scratchKey = ComputeTextureScratchKey(backendTexture.width(), backendTexture.height(),
                                          texture->format(), texture->mipLevelCount() > 1);
  }
  auto textureView = new DefaultTextureView(std::move(texture), origin);
  return Resource::AddToCache(context, textureView, scratchKey);
}

std::shared_ptr<TextureView> TextureView::MakeFrom(Context* context,
                                                   std::shared_ptr<ImageBuffer> imageBuffer,
                                                   bool mipmapped) {
  if (context == nullptr || imageBuffer == nullptr) {
    return nullptr;
  }
  return imageBuffer->onMakeTexture(context, mipmapped);
}

std::shared_ptr<TextureView> TextureView::MakeFrom(Context* context,
                                                   HardwareBufferRef hardwareBuffer,
                                                   YUVColorSpace colorSpace) {
  if (context == nullptr) {
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

  auto textures =
      context->gpu()->importHardwareTextures(hardwareBuffer, GPUTextureUsage::TEXTURE_BINDING);
  if (textures.empty()) {
    return nullptr;
  }
  TextureView* textureView = nullptr;
  if (textures.size() == 1) {
    textureView = new DefaultTextureView(std::move(textures.front()));
  } else {
    YUVFormat yuvFormat = YUVFormat::Unknown;
    context->gpu()->getHardwareTextureFormats(hardwareBuffer, &yuvFormat);
    DEBUG_ASSERT(yuvFormat != YUVFormat::Unknown);
    textureView = new YUVTextureView(std::move(textures), yuvFormat, colorSpace);
  }
  return Resource::AddToCache(context, textureView);
}

Point TextureView::getTextureCoord(float x, float y) const {
  if (getTexture()->type() == GPUTextureType::Rectangle) {
    return {x, y};
  }
  return {x / static_cast<float>(width()), y / static_cast<float>(height())};
}
}  // namespace tgfx

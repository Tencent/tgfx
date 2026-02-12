/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "MtlHardwareTexture.h"
#include "MtlGPU.h"

namespace tgfx {

static std::shared_ptr<Texture> CreateTextureOfPlane(MtlGPU* gpu, CVPixelBufferRef pixelBuffer,
                                                     size_t planeIndex, PixelFormat pixelFormat,
                                                     uint32_t usage,
                                                     CVMetalTextureCacheRef textureCache) {
  auto planeCount = CVPixelBufferGetPlaneCount(pixelBuffer);
  size_t width = 0;
  size_t height = 0;
  if (planeCount > 0) {
    width = CVPixelBufferGetWidthOfPlane(pixelBuffer, planeIndex);
    height = CVPixelBufferGetHeightOfPlane(pixelBuffer, planeIndex);
  } else {
    width = CVPixelBufferGetWidth(pixelBuffer);
    height = CVPixelBufferGetHeight(pixelBuffer);
  }
  MTLPixelFormat mtlPixelFormat = MTLPixelFormatInvalid;
  switch (pixelFormat) {
    case PixelFormat::ALPHA_8:
    case PixelFormat::GRAY_8:
      mtlPixelFormat = MTLPixelFormatR8Unorm;
      break;
    case PixelFormat::RG_88:
      mtlPixelFormat = MTLPixelFormatRG8Unorm;
      break;
    case PixelFormat::BGRA_8888:
      mtlPixelFormat = MTLPixelFormatBGRA8Unorm;
      break;
    case PixelFormat::RGBA_8888:
      mtlPixelFormat = MTLPixelFormatRGBA8Unorm;
      break;
    default:
      return nullptr;
  }

  CVMetalTextureRef metalTexture = nil;
  CVReturn result = CVMetalTextureCacheCreateTextureFromImage(
      kCFAllocatorDefault, textureCache, pixelBuffer, nil, mtlPixelFormat, width, height,
      planeIndex, &metalTexture);
  if (result != kCVReturnSuccess || metalTexture == nil) {
    return nullptr;
  }

  id<MTLTexture> mtlTexture = CVMetalTextureGetTexture(metalTexture);
  if (mtlTexture == nil) {
    CFRelease(metalTexture);
    return nullptr;
  }

  TextureDescriptor descriptor = {static_cast<int>(width), static_cast<int>(height), pixelFormat,
                                  false, 1, usage};
  return gpu->makeResource<MtlHardwareTexture>(descriptor, mtlTexture, pixelBuffer,
                                               metalTexture);
}

std::vector<std::shared_ptr<Texture>> MtlHardwareTexture::MakeFrom(
    MtlGPU* gpu, CVPixelBufferRef pixelBuffer, uint32_t usage,
    CVMetalTextureCacheRef textureCache) {
  if (textureCache == nil || pixelBuffer == nil) {
    return {};
  }

  auto pixelFormat = CVPixelBufferGetPixelFormatType(pixelBuffer);
  std::vector<PixelFormat> formats = {};
  switch (pixelFormat) {
    case kCVPixelFormatType_OneComponent8:
      formats.push_back(PixelFormat::ALPHA_8);
      break;
    case kCVPixelFormatType_32BGRA:
      formats.push_back(PixelFormat::BGRA_8888);
      break;
    case kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange:
    case kCVPixelFormatType_420YpCbCr8BiPlanarFullRange:
      formats.push_back(PixelFormat::GRAY_8);
      formats.push_back(PixelFormat::RG_88);
      break;
    default:
      break;
  }
  if (formats.empty()) {
    return {};
  }
  if (usage & TextureUsage::RENDER_ATTACHMENT &&
      (formats.size() != 1 || !gpu->isFormatRenderable(formats.front()))) {
    return {};
  }

  std::vector<std::shared_ptr<Texture>> textures = {};
  for (size_t i = 0; i < formats.size(); ++i) {
    auto texture = CreateTextureOfPlane(gpu, pixelBuffer, i, formats[i], usage, textureCache);
    if (texture == nullptr) {
      return {};
    }
    textures.push_back(std::move(texture));
  }
  return textures;
}

MtlHardwareTexture::MtlHardwareTexture(const TextureDescriptor& descriptor,
                                       id<MTLTexture> mtlTexture, CVPixelBufferRef pixelBuffer,
                                       CVMetalTextureRef metalTexture)
    : MtlTexture(descriptor, mtlTexture),
      pixelBuffer(pixelBuffer),
      metalTexture(metalTexture) {
  CFRetain(pixelBuffer);
}

void MtlHardwareTexture::onRelease(MtlGPU* gpu) {
  CFRelease(pixelBuffer);
  pixelBuffer = nullptr;
  if (metalTexture != nil) {
    CFRelease(metalTexture);
    metalTexture = nil;
  }
  MtlTexture::onRelease(gpu);
}

void MtlHardwareTexture::onReleaseTexture() {
  // CVMetalTextureGetTexture returns an autoreleased texture that is backed by metalTexture.
  // We should not release it directly; it will be released when metalTexture is released.
  texture = nil;
}

}  // namespace tgfx

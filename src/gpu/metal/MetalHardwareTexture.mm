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

#include "MetalHardwareTexture.h"
#include "MetalGPU.h"

namespace tgfx {

static std::shared_ptr<Texture> CreateTextureOfPlane(MetalGPU* gpu, CVPixelBufferRef pixelBuffer,
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
  MTLPixelFormat metalPixelFormat = MTLPixelFormatInvalid;
  switch (pixelFormat) {
    case PixelFormat::ALPHA_8:
    case PixelFormat::GRAY_8:
      metalPixelFormat = MTLPixelFormatR8Unorm;
      break;
    case PixelFormat::RG_88:
      metalPixelFormat = MTLPixelFormatRG8Unorm;
      break;
    case PixelFormat::BGRA_8888:
      metalPixelFormat = MTLPixelFormatBGRA8Unorm;
      break;
    case PixelFormat::RGBA_8888:
      metalPixelFormat = MTLPixelFormatRGBA8Unorm;
      break;
    default:
      return nullptr;
  }

  CVMetalTextureRef cvTexture = nil;
  CVReturn result = CVMetalTextureCacheCreateTextureFromImage(
      kCFAllocatorDefault, textureCache, pixelBuffer, nil, metalPixelFormat, width, height,
      planeIndex, &cvTexture);
  if (result != kCVReturnSuccess || cvTexture == nil) {
    return nullptr;
  }

  id<MTLTexture> mtlTexture = CVMetalTextureGetTexture(cvTexture);
  if (mtlTexture == nil) {
    CFRelease(cvTexture);
    return nullptr;
  }

  TextureDescriptor descriptor = {static_cast<int>(width), static_cast<int>(height), pixelFormat,
                                  false, 1, usage};
  return gpu->makeResource<MetalHardwareTexture>(descriptor, mtlTexture, pixelBuffer,
                                               cvTexture);
}

std::vector<std::shared_ptr<Texture>> MetalHardwareTexture::MakeFrom(
    MetalGPU* gpu, CVPixelBufferRef pixelBuffer, uint32_t usage,
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

MetalHardwareTexture::MetalHardwareTexture(const TextureDescriptor& descriptor,
                                       id<MTLTexture> mtlTexture, CVPixelBufferRef pixelBuffer,
                                       CVMetalTextureRef cvMetalTexture)
    : MetalTexture(descriptor, mtlTexture),
      pixelBuffer(pixelBuffer),
      cvMetalTexture(cvMetalTexture) {
  CFRetain(pixelBuffer);
}

void MetalHardwareTexture::onRelease(MetalGPU* gpu) {
  if (cvMetalTexture != nil) {
    CFRelease(cvMetalTexture);
    cvMetalTexture = nil;
  }
  CFRelease(pixelBuffer);
  pixelBuffer = nullptr;
  MetalTexture::onRelease(gpu);
}

void MetalHardwareTexture::onReleaseTexture() {
  // CVMetalTextureGetTexture returns an autoreleased texture that is backed by cvMetalTexture.
  // We should not release it directly; it will be released when cvMetalTexture is released.
  texture = nil;
}

}  // namespace tgfx

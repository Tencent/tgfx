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

#include "EAGLHardwareTextureSampler.h"
#include "gpu/opengl/GLCaps.h"
#include "tgfx/gpu/opengl/eagl/EAGLDevice.h"

namespace tgfx {
static std::unique_ptr<TextureSampler> CreateSamplerOfPlane(
    Context* context, CVPixelBufferRef pixelBuffer, size_t planeIndex, PixelFormat pixelFormat,
    CVOpenGLESTextureCacheRef textureCache) {
  auto width = static_cast<int>(CVPixelBufferGetWidthOfPlane(pixelBuffer, planeIndex));
  auto height = static_cast<int>(CVPixelBufferGetHeightOfPlane(pixelBuffer, planeIndex));
  CVOpenGLESTextureRef texture = nil;
  auto caps = GLCaps::Get(context);
  const auto& format = caps->getTextureFormat(pixelFormat);
  // The returned texture object has a strong reference count of 1.
  CVReturn result = CVOpenGLESTextureCacheCreateTextureFromImage(
      kCFAllocatorDefault, textureCache, pixelBuffer, NULL,             /* texture attributes */
      GL_TEXTURE_2D, static_cast<GLint>(format.internalFormatTexImage), /* opengl format */
      width, height, format.externalFormat,                             /* native iOS format */
      GL_UNSIGNED_BYTE, planeIndex, &texture);
  if (result != kCVReturnSuccess && texture != nil) {
    CFRelease(texture);
    return nullptr;
  }
  auto samplerID = CVOpenGLESTextureGetName(texture);
  auto target = CVOpenGLESTextureGetTarget(texture);
  auto sampler = std::unique_ptr<EAGLHardwareTextureSampler>(
      new EAGLHardwareTextureSampler(pixelBuffer, texture, samplerID, target, pixelFormat));
  return sampler;
}

std::vector<std::unique_ptr<TextureSampler>> EAGLHardwareTextureSampler::MakeFrom(
    Context* context, CVPixelBufferRef pixelBuffer) {
  auto textureCache = static_cast<EAGLDevice*>(context->device())->getTextureCache();
  if (textureCache == nil) {
    return {};
  }
  auto pixelFormat = CVPixelBufferGetPixelFormatType(pixelBuffer);
  std::vector<PixelFormat> planeFormats = {};
  switch (pixelFormat) {
    case kCVPixelFormatType_OneComponent8:
      planeFormats.push_back(PixelFormat::ALPHA_8);
      break;
    case kCVPixelFormatType_32BGRA:
      planeFormats.push_back(PixelFormat::BGRA_8888);
      break;
    case kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange:
    case kCVPixelFormatType_420YpCbCr8BiPlanarFullRange:
      planeFormats.push_back(PixelFormat::GRAY_8);
      planeFormats.push_back(PixelFormat::RG_88);
      break;
    default:
      return {};  // Unsupported pixel format
  }
  std::vector<std::unique_ptr<TextureSampler>> samplers = {};
  for (size_t i = 0; i < planeFormats.size(); ++i) {
    auto sampler = CreateSamplerOfPlane(context, pixelBuffer, i, planeFormats[i], textureCache);
    if (sampler == nullptr) {
      for (auto& plane : samplers) {
        plane->releaseGPU(context);
      }
      return {};
    }
    samplers.push_back(std::move(sampler));
  }
  return samplers;
}

EAGLHardwareTextureSampler::EAGLHardwareTextureSampler(CVPixelBufferRef pixelBuffer,
                                                       CVOpenGLESTextureRef texture, unsigned id,
                                                       unsigned target, PixelFormat format)
    : GLTextureSampler(id, target, format), pixelBuffer(pixelBuffer), texture(texture) {
  CFRetain(pixelBuffer);
}

EAGLHardwareTextureSampler::~EAGLHardwareTextureSampler() {
  CFRelease(pixelBuffer);
  if (texture) {
    CFRelease(texture);
  }
}

void EAGLHardwareTextureSampler::releaseGPU(Context* context) {
  if (texture == nil) {
    return;
  }
  CFRelease(texture);
  texture = nil;
  auto textureCache = static_cast<EAGLDevice*>(context->device())->getTextureCache();
  if (textureCache != nil) {
    CVOpenGLESTextureCacheFlush(textureCache, 0);
  }
}
}  // namespace tgfx

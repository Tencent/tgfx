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

#include "EAGLHardwareTexture.h"
#include "gpu/opengl/GLCaps.h"
#include "tgfx/gpu/opengl/eagl/EAGLDevice.h"

namespace tgfx {
static std::shared_ptr<GPUTexture> CreateTextureOfPlane(EAGLGPU* gpu, CVPixelBufferRef pixelBuffer,
                                                        size_t planeIndex, PixelFormat pixelFormat,
                                                        uint32_t usage,
                                                        CVOpenGLESTextureCacheRef textureCache) {
  auto width = static_cast<int>(CVPixelBufferGetWidthOfPlane(pixelBuffer, planeIndex));
  auto height = static_cast<int>(CVPixelBufferGetHeightOfPlane(pixelBuffer, planeIndex));
  CVOpenGLESTextureRef texture = nil;
  auto caps = static_cast<const GLCaps*>(gpu->caps());
  const auto& format = caps->getTextureFormat(pixelFormat);
  // The returned texture object has a strong reference count of 1.
  CVReturn result = CVOpenGLESTextureCacheCreateTextureFromImage(
      kCFAllocatorDefault, textureCache, pixelBuffer, NULL,             /* texture attributes */
      GL_TEXTURE_2D, static_cast<GLint>(format.internalFormatTexImage), /* opengl format */
      width, height, format.externalFormat,                             /* native iOS format */
      format.externalType, planeIndex, &texture);
  if (result != kCVReturnSuccess && texture != nil) {
    CFRelease(texture);
    return nullptr;
  }
  auto textureID = CVOpenGLESTextureGetName(texture);
  auto target = CVOpenGLESTextureGetTarget(texture);
  GPUTextureDescriptor descriptor = {static_cast<int>(CVPixelBufferGetWidth(pixelBuffer)),
                                     static_cast<int>(CVPixelBufferGetHeight(pixelBuffer)),
                                     pixelFormat,
                                     false,
                                     1,
                                     usage};
  auto hardwareTexture =
      gpu->makeResource<EAGLHardwareTexture>(descriptor, pixelBuffer, texture, target, textureID);
  if (hardwareTexture->usage() & GPUTextureUsage::RENDER_ATTACHMENT &&
      !hardwareTexture->checkFrameBuffer(gpu)) {
    return nullptr;
  }
  return hardwareTexture;
}

std::vector<std::shared_ptr<GPUTexture>> EAGLHardwareTexture::MakeFrom(EAGLGPU* gpu,
                                                                       CVPixelBufferRef pixelBuffer,
                                                                       uint32_t usage) {
  auto textureCache = gpu->getTextureCache();
  if (textureCache == nil) {
    return {};
  }
  auto yuvFormat = YUVFormat::Unknown;
  auto formats = gpu->getHardwareTextureFormats(pixelBuffer, &yuvFormat);
  if (formats.empty()) {
    return {};
  }
  if (usage & GPUTextureUsage::RENDER_ATTACHMENT &&
      (yuvFormat != YUVFormat::Unknown || !gpu->isFormatRenderable(formats.front()))) {
    return {};
  }
  std::vector<std::shared_ptr<GPUTexture>> textures = {};
  for (size_t i = 0; i < formats.size(); ++i) {
    auto texture = CreateTextureOfPlane(gpu, pixelBuffer, i, formats[i], usage, textureCache);
    if (texture == nullptr) {
      return {};
    }
    textures.push_back(std::move(texture));
  }
  return textures;
}

EAGLHardwareTexture::EAGLHardwareTexture(const GPUTextureDescriptor& descriptor,
                                         CVPixelBufferRef pixelBuffer, CVOpenGLESTextureRef texture,
                                         unsigned target, unsigned textureID)
    : GLTexture(descriptor, target, textureID), pixelBuffer(pixelBuffer), texture(texture) {
  CFRetain(pixelBuffer);
}

EAGLHardwareTexture::~EAGLHardwareTexture() {
  CFRelease(pixelBuffer);
  if (texture) {
    CFRelease(texture);
  }
}

void EAGLHardwareTexture::onReleaseTexture(GLGPU* gpu) {
  if (texture == nil) {
    return;
  }
  CFRelease(texture);
  texture = nil;
  auto textureCache = static_cast<EAGLGPU*>(gpu)->getTextureCache();
  if (textureCache != nil) {
    CVOpenGLESTextureCacheFlush(textureCache, 0);
  }
}
}  // namespace tgfx

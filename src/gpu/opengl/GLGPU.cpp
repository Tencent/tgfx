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

#include "GLGPU.h"
#include "gpu/opengl/GLBuffer.h"
#include "gpu/opengl/GLCommandEncoder.h"
#include "gpu/opengl/GLExternalFrameBuffer.h"
#include "gpu/opengl/GLExternalTexture.h"
#include "gpu/opengl/GLTextureFrameBuffer.h"
#include "gpu/opengl/GLUtil.h"

namespace tgfx {
GLGPU::GLGPU(std::shared_ptr<GLInterface> glInterface) : interface(std::move(glInterface)) {
  commandQueue = std::make_unique<GLCommandQueue>(interface);
}

std::unique_ptr<GPUBuffer> GLGPU::createBuffer(size_t size, uint32_t usage) {
  if (size == 0) {
    return nullptr;
  }
  unsigned target = 0;
  if (usage & GPUBufferUsage::VERTEX) {
    target = GL_ARRAY_BUFFER;
  } else if (usage & GPUBufferUsage::INDEX) {
    target = GL_ELEMENT_ARRAY_BUFFER;
  } else {
    LOGE("GLGPU::createBuffer() invalid buffer usage!");
    return nullptr;
  }
  auto gl = interface->functions();
  unsigned bufferID = 0;
  gl->genBuffers(1, &bufferID);
  if (bufferID == 0) {
    return nullptr;
  }
  gl->bindBuffer(target, bufferID);
  gl->bufferData(target, static_cast<GLsizeiptr>(size), nullptr, GL_STATIC_DRAW);
  gl->bindBuffer(target, 0);
  return std::make_unique<GLBuffer>(bufferID, size, usage);
}

std::unique_ptr<GPUTexture> GLGPU::createTexture(int width, int height, PixelFormat format,
                                                 bool mipmapped) {
  auto gl = functions();
  // Clear the previously generated GLError, causing the subsequent CheckGLError to return an
  // incorrect result.
  ClearGLError(gl);
  unsigned target = GL_TEXTURE_2D;
  unsigned textureID = 0;
  gl->genTextures(1, &textureID);
  if (textureID == 0) {
    return nullptr;
  }
  gl->bindTexture(target, textureID);
  gl->texParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  gl->texParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  gl->texParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  gl->texParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  auto glCaps = interface->caps();
  auto& textureFormat = glCaps->getTextureFormat(format);
  int maxMipmapLevel = mipmapped ? glCaps->getMaxMipmapLevel(width, height) : 0;
  bool success = true;
  // Texture memory must be allocated first on the web platform then can write pixels.
  for (int level = 0; level <= maxMipmapLevel && success; level++) {
    const int twoToTheMipLevel = 1 << level;
    const int currentWidth = std::max(1, width / twoToTheMipLevel);
    const int currentHeight = std::max(1, height / twoToTheMipLevel);
    gl->texImage2D(target, level, static_cast<int>(textureFormat.internalFormatTexImage),
                   currentWidth, currentHeight, 0, textureFormat.externalFormat, GL_UNSIGNED_BYTE,
                   nullptr);
    success = CheckGLError(gl);
  }
  if (!success) {
    gl->deleteTextures(1, &textureID);
    return nullptr;
  }
  return std::make_unique<GLTexture>(textureID, target, format, maxMipmapLevel);
}

PixelFormat GLGPU::getExternalTextureFormat(const BackendTexture& backendTexture) const {
  GLTextureInfo textureInfo = {};
  if (!backendTexture.isValid() || !backendTexture.getGLTextureInfo(&textureInfo)) {
    return PixelFormat::Unknown;
  }
  return GLSizeFormatToPixelFormat(textureInfo.format);
}

std::unique_ptr<GPUTexture> GLGPU::importExternalTexture(const BackendTexture& backendTexture,
                                                         bool adopted) {
  GLTextureInfo textureInfo = {};
  if (!backendTexture.getGLTextureInfo(&textureInfo)) {
    return nullptr;
  }
  auto format = GLSizeFormatToPixelFormat(textureInfo.format);
  if (adopted) {
    return std::make_unique<GLTexture>(textureInfo.id, textureInfo.target, format);
  }
  return std::make_unique<GLExternalTexture>(textureInfo.id, textureInfo.target, format);
}

std::unique_ptr<GPUFrameBuffer> GLGPU::createFrameBuffer(GPUTexture* texture, int width, int height,
                                                         int sampleCount) {
  return GLTextureFrameBuffer::MakeFrom(this, texture, width, height, sampleCount);
}

PixelFormat GLGPU::getExternalFrameBufferFormat(const BackendRenderTarget& renderTarget) const {
  GLFrameBufferInfo frameBufferInfo = {};
  if (!renderTarget.getGLFramebufferInfo(&frameBufferInfo)) {
    return PixelFormat::Unknown;
  }
  return GLSizeFormatToPixelFormat(frameBufferInfo.format);
}

std::unique_ptr<GPUFrameBuffer> GLGPU::importExternalFrameBuffer(
    const BackendRenderTarget& renderTarget) {
  GLFrameBufferInfo frameBufferInfo = {};
  if (!renderTarget.getGLFramebufferInfo(&frameBufferInfo)) {
    return nullptr;
  }
  auto format = GLSizeFormatToPixelFormat(frameBufferInfo.format);
  if (!caps()->isFormatRenderable(format)) {
    return nullptr;
  }
  return std::unique_ptr<GLExternalFrameBuffer>(
      new GLExternalFrameBuffer(frameBufferInfo.id, format));
}

std::shared_ptr<CommandEncoder> GLGPU::createCommandEncoder() {
  return std::make_shared<GLCommandEncoder>(interface);
}
}  // namespace tgfx

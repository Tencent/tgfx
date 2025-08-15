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
#include "gpu/opengl/GLExternalTexture.h"
#include "gpu/opengl/GLMultisampleTexture.h"
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

std::unique_ptr<GPUTexture> GLGPU::createTexture(const GPUTextureDescriptor& descriptor) {
  if (descriptor.width <= 0 || descriptor.height <= 0 ||
      descriptor.format == PixelFormat::Unknown || descriptor.mipLevelCount < 1 ||
      descriptor.sampleCount < 1 || descriptor.usage == 0) {
    LOGE("GLGPU::createTexture() invalid texture descriptor!");
    return nullptr;
  }
  if (descriptor.sampleCount > 1) {
    return GLMultisampleTexture::MakeFrom(this, descriptor);
  }
  if (descriptor.usage & GPUTextureUsage::RENDER_ATTACHMENT &&
      !caps()->isFormatRenderable(descriptor.format)) {
    LOGE("GLGPU::createTexture() format is not renderable, but usage includes RENDER_ATTACHMENT!");
    return nullptr;
  }
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
  auto& textureFormat = interface->caps()->getTextureFormat(descriptor.format);
  bool success = true;
  // Texture memory must be allocated first on the web platform then can write pixels.
  for (int level = 0; level < descriptor.mipLevelCount && success; level++) {
    const int twoToTheMipLevel = 1 << level;
    const int currentWidth = std::max(1, descriptor.width / twoToTheMipLevel);
    const int currentHeight = std::max(1, descriptor.height / twoToTheMipLevel);
    gl->texImage2D(target, level, static_cast<int>(textureFormat.internalFormatTexImage),
                   currentWidth, currentHeight, 0, textureFormat.externalFormat, GL_UNSIGNED_BYTE,
                   nullptr);
    success = CheckGLError(gl);
  }
  if (!success) {
    gl->deleteTextures(1, &textureID);
    return nullptr;
  }
  auto texture = std::make_unique<GLTexture>(descriptor, target, textureID);
  if (!texture->checkFrameBuffer(this)) {
    texture->release(this);
    return nullptr;
  }
  return texture;
}

PixelFormat GLGPU::getExternalTextureFormat(const BackendTexture& backendTexture) const {
  GLTextureInfo textureInfo = {};
  if (!backendTexture.isValid() || !backendTexture.getGLTextureInfo(&textureInfo)) {
    return PixelFormat::Unknown;
  }
  return GLSizeFormatToPixelFormat(textureInfo.format);
}

std::unique_ptr<GPUTexture> GLGPU::importExternalTexture(const BackendTexture& backendTexture,
                                                         uint32_t usage, bool adopted) {
  GLTextureInfo textureInfo = {};
  if (!backendTexture.getGLTextureInfo(&textureInfo)) {
    return nullptr;
  }
  auto format = GLSizeFormatToPixelFormat(textureInfo.format);
  if (usage & GPUTextureUsage::RENDER_ATTACHMENT && !caps()->isFormatRenderable(format)) {
    LOGE(
        "GLGPU::importExternalTexture() format is not renderable but RENDER_ATTACHMENT usage is "
        "set!");
    return nullptr;
  }
  GPUTextureDescriptor descriptor = {};
  descriptor.width = backendTexture.width();
  descriptor.height = backendTexture.height();
  descriptor.format = format;
  descriptor.usage = usage;
  std::unique_ptr<GLTexture> texture = nullptr;
  if (adopted) {
    texture = std::make_unique<GLTexture>(descriptor, textureInfo.target, textureInfo.id);
  } else {
    texture = std::make_unique<GLExternalTexture>(descriptor, textureInfo.target, textureInfo.id);
  }
  if (!texture->checkFrameBuffer(this)) {
    texture->release(this);
    return nullptr;
  }
  return texture;
}

PixelFormat GLGPU::getExternalTextureFormat(const BackendRenderTarget& renderTarget) const {
  GLFrameBufferInfo frameBufferInfo = {};
  if (!renderTarget.getGLFramebufferInfo(&frameBufferInfo)) {
    return PixelFormat::Unknown;
  }
  return GLSizeFormatToPixelFormat(frameBufferInfo.format);
}

std::unique_ptr<GPUTexture> GLGPU::importExternalTexture(const BackendRenderTarget& renderTarget) {
  GLFrameBufferInfo frameBufferInfo = {};
  if (!renderTarget.getGLFramebufferInfo(&frameBufferInfo)) {
    return nullptr;
  }
  auto format = GLSizeFormatToPixelFormat(frameBufferInfo.format);
  if (!caps()->isFormatRenderable(format)) {
    return nullptr;
  }
  GPUTextureDescriptor descriptor = {};
  descriptor.width = renderTarget.width();
  descriptor.height = renderTarget.height();
  descriptor.format = format;
  descriptor.usage = GPUTextureUsage::RENDER_ATTACHMENT;
  return std::make_unique<GLExternalTexture>(descriptor, GL_TEXTURE_2D, 0, frameBufferInfo.id);
}

std::shared_ptr<CommandEncoder> GLGPU::createCommandEncoder() {
  return std::make_shared<GLCommandEncoder>(interface);
}
}  // namespace tgfx

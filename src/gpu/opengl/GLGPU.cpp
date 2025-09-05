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
#include "gpu/opengl/GLFence.h"
#include "gpu/opengl/GLMultisampleTexture.h"
#include "gpu/opengl/GLSampler.h"
#include "gpu/opengl/GLUtil.h"

namespace tgfx {
GLGPU::GLGPU(std::shared_ptr<GLInterface> glInterface) : interface(std::move(glInterface)) {
  commandQueue = std::make_unique<GLCommandQueue>(this);
  textureUnits.resize(static_cast<size_t>(interface->caps()->maxFragmentSamplers), 0);
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
  if (descriptor.mipLevelCount > 1 && !caps()->mipmapSupport) {
    LOGE("GLGPU::createTexture() mipmaps are not supported!");
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
  auto texture = std::make_unique<GLTexture>(descriptor, target, textureID);
  bindTexture(texture.get());
  auto& textureFormat = interface->caps()->getTextureFormat(descriptor.format);
  bool success = true;
  // Texture memory must be allocated first on the web platform then can write pixels.
  for (int level = 0; level < descriptor.mipLevelCount && success; level++) {
    auto twoToTheMipLevel = 1 << level;
    auto currentWidth = std::max(1, descriptor.width / twoToTheMipLevel);
    auto currentHeight = std::max(1, descriptor.height / twoToTheMipLevel);
    gl->texImage2D(target, level, static_cast<int>(textureFormat.internalFormatTexImage),
                   currentWidth, currentHeight, 0, textureFormat.externalFormat, GL_UNSIGNED_BYTE,
                   nullptr);
    success = CheckGLError(gl);
  }
  if (!success) {
    texture->release(this);
    return nullptr;
  }
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
  GPUTextureDescriptor descriptor = {
      backendTexture.width(), backendTexture.height(), format, false, 1, usage};
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
  GPUTextureDescriptor descriptor = {renderTarget.width(),
                                     renderTarget.height(),
                                     format,
                                     false,
                                     1,
                                     GPUTextureUsage::RENDER_ATTACHMENT};
  return std::make_unique<GLExternalTexture>(descriptor, GL_TEXTURE_2D, 0, frameBufferInfo.id);
}

std::unique_ptr<GPUFence> GLGPU::importExternalFence(const BackendSemaphore& semaphore) {
  GLSyncInfo glSyncInfo = {};
  if (!caps()->semaphoreSupport || !semaphore.getGLSync(&glSyncInfo)) {
    return nullptr;
  }
  return std::make_unique<GLFence>(glSyncInfo.sync);
}

static int ToGLWrap(AddressMode wrapMode) {
  switch (wrapMode) {
    case AddressMode::ClampToEdge:
      return GL_CLAMP_TO_EDGE;
    case AddressMode::Repeat:
      return GL_REPEAT;
    case AddressMode::MirrorRepeat:
      return GL_MIRRORED_REPEAT;
    case AddressMode::ClampToBorder:
      return GL_CLAMP_TO_BORDER;
  }
  return GL_REPEAT;
}

std::unique_ptr<GPUSampler> GLGPU::createSampler(const GPUSamplerDescriptor& descriptor) {
  int minFilter = GL_LINEAR;
  switch (descriptor.mipmapMode) {
    case MipmapMode::None:
      minFilter = descriptor.minFilter == FilterMode::Nearest ? GL_NEAREST : GL_LINEAR;
      break;
    case MipmapMode::Nearest:
      minFilter = descriptor.minFilter == FilterMode::Nearest ? GL_NEAREST_MIPMAP_NEAREST
                                                              : GL_LINEAR_MIPMAP_NEAREST;
      break;
    case MipmapMode::Linear:
      minFilter = descriptor.minFilter == FilterMode::Nearest ? GL_NEAREST_MIPMAP_LINEAR
                                                              : GL_LINEAR_MIPMAP_LINEAR;
      break;
  }
  int magFilter = descriptor.magFilter == FilterMode::Nearest ? GL_NEAREST : GL_LINEAR;
  return std::make_unique<GLSampler>(ToGLWrap(descriptor.addressModeX),
                                     ToGLWrap(descriptor.addressModeY), minFilter, magFilter);
}

std::shared_ptr<CommandEncoder> GLGPU::createCommandEncoder() {
  return std::make_shared<GLCommandEncoder>(this);
}

void GLGPU::resetGLState() {
  activeTextureUint = UINT_MAX;
  memset(textureUnits.data(), 0, textureUnits.size() * sizeof(textureUnits[0]));
  readFramebuffer = UINT_MAX;
  drawFramebuffer = UINT_MAX;
}

void GLGPU::bindTexture(GLTexture* texture, unsigned textureUnit) {
  DEBUG_ASSERT(texture != nullptr);
  DEBUG_ASSERT(texture->usage() & GPUTextureUsage::TEXTURE_BINDING);
  auto& uniqueID = textureUnits[textureUnit];
  if (uniqueID == texture->uniqueID) {
    return;
  }

  auto gl = interface->functions();
  if (activeTextureUint != textureUnit) {
    gl->activeTexture(static_cast<unsigned>(GL_TEXTURE0) + textureUnit);
    activeTextureUint = textureUnit;
  }
  gl->bindTexture(texture->target(), texture->textureID());
  uniqueID = texture->uniqueID;
}

void GLGPU::bindFramebuffer(GLTexture* texture, FrameBufferTarget target) {
  DEBUG_ASSERT(texture != nullptr);
  DEBUG_ASSERT(texture->usage() & GPUTextureUsage::RENDER_ATTACHMENT);
  auto uniqueID = texture->uniqueID;
  unsigned frameBufferTarget = GL_FRAMEBUFFER;
  switch (target) {
    case FrameBufferTarget::Read:
      if (uniqueID == readFramebuffer) {
        return;
      }
      frameBufferTarget = GL_READ_FRAMEBUFFER;
      readFramebuffer = texture->uniqueID;
      break;
    case FrameBufferTarget::Draw:
      if (uniqueID == drawFramebuffer) {
        return;
      }
      frameBufferTarget = GL_DRAW_FRAMEBUFFER;
      drawFramebuffer = texture->uniqueID;
      break;
    case FrameBufferTarget::Both:
      if (uniqueID == drawFramebuffer && uniqueID == readFramebuffer) {
        return;
      }
      frameBufferTarget = GL_FRAMEBUFFER;
      readFramebuffer = drawFramebuffer = texture->uniqueID;
      break;
  }
  auto gl = interface->functions();
  gl->bindFramebuffer(frameBufferTarget, texture->frameBufferID());
}

}  // namespace tgfx

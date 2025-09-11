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
#include "gpu/opengl/GLRenderPipeline.h"
#include "gpu/opengl/GLSampler.h"
#include "gpu/opengl/GLShaderModule.h"
#include "gpu/opengl/GLUtil.h"

namespace tgfx {
GLGPU::GLGPU(std::shared_ptr<GLInterface> glInterface) : interface(std::move(glInterface)) {
  commandQueue = std::make_unique<GLCommandQueue>(this);
  auto shaderCaps = interface->caps()->shaderCaps();
  textureUnits.resize(static_cast<size_t>(shaderCaps->maxFragmentSamplers), INVALID_VALUE);
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
  if (descriptor.usage & GPUTextureUsage::RENDER_ATTACHMENT && !texture->checkFrameBuffer(this)) {
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
  if (usage & GPUTextureUsage::RENDER_ATTACHMENT && !texture->checkFrameBuffer(this)) {
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

std::unique_ptr<GPUShaderModule> GLGPU::createShaderModule(
    const GPUShaderModuleDescriptor& descriptor) {
  if (descriptor.code.empty()) {
    LOGE("GLGPU::createShaderModule() shader code is empty!");
    return nullptr;
  }
  unsigned shaderType = 0;
  switch (descriptor.stage) {
    case ShaderStage::Vertex:
      shaderType = GL_VERTEX_SHADER;
      break;
    case ShaderStage::Fragment:
      shaderType = GL_FRAGMENT_SHADER;
      break;
  }
  auto gl = interface->functions();
  auto shader = gl->createShader(shaderType);
  auto& code = descriptor.code;
  const char* files[] = {descriptor.code.c_str()};
  gl->shaderSource(shader, 1, files, nullptr);
  gl->compileShader(shader);
#if defined(DEBUG) || !defined(TGFX_BUILD_FOR_WEB)
  int success;
  gl->getShaderiv(shader, GL_COMPILE_STATUS, &success);
  if (!success) {
    char infoLog[512];
    gl->getShaderInfoLog(shader, 512, nullptr, infoLog);
    LOGE("Could not compile shader:\n%s\ntype:%d info%s", code.c_str(), shaderType, infoLog);
    gl->deleteShader(shader);
    shader = 0;
  }
#endif
  return std::make_unique<GLShaderModule>(shader);
}

std::unique_ptr<GPURenderPipeline> GLGPU::createRenderPipeline(
    const GPURenderPipelineDescriptor& descriptor) {
  auto vertexModule = static_cast<GLShaderModule*>(descriptor.vertex.module);
  auto fragmentModule = static_cast<GLShaderModule*>(descriptor.fragment.module);
  if (vertexModule == nullptr || vertexModule->shader() == 0 || vertexModule == nullptr ||
      fragmentModule->shader() == 0) {
    LOGE("GLGPU::createRenderPipeline() invalid shader module!");
    return nullptr;
  }
  if (descriptor.vertex.attributes.empty()) {
    LOGE("GLGPU::createRenderPipeline() invalid vertex attributes, no attributes set!");
    return nullptr;
  }
  if (descriptor.vertex.vertexStride == 0) {
    LOGE("GLGPU::createRenderPipeline() invalid vertex attributes, vertex stride is 0!");
    return nullptr;
  }
  if (descriptor.fragment.colorAttachments.empty()) {
    LOGE("GLGPU::createRenderPipeline() invalid color attachments, no color attachments!");
    return nullptr;
  }
  if (descriptor.fragment.colorAttachments.size() > 1) {
    LOGE(
        "GLGPU::createRenderPipeline() Multiple color attachments are not yet supported in "
        "OpenGL!");
    return nullptr;
  }
  auto gl = interface->functions();
  auto programID = gl->createProgram();
  gl->attachShader(programID, vertexModule->shader());
  gl->attachShader(programID, fragmentModule->shader());
  gl->linkProgram(programID);
  int success;
  gl->getProgramiv(programID, GL_LINK_STATUS, &success);
  if (!success) {
    char infoLog[512];
    gl->getProgramInfoLog(programID, 512, nullptr, infoLog);
    gl->deleteProgram(programID);
    programID = 0;
    LOGE("GLGPU::createRenderPipeline() Could not link program: %s", infoLog);
  }
  auto pipeline = std::make_unique<GLRenderPipeline>(programID);
  if (!pipeline->setPipelineDescriptor(this, descriptor)) {
    pipeline->release(this);
    return nullptr;
  }
  return pipeline;
}

std::shared_ptr<CommandEncoder> GLGPU::createCommandEncoder() {
  return std::make_shared<GLCommandEncoder>(this);
}

void GLGPU::resetGLState() {
  capabilities = {};
  scissorRect = {0, 0, 0, 0};
  viewport = {0, 0, 0, 0};
  textureUnits.assign(textureUnits.size(), INVALID_VALUE);
  clearColor = std::nullopt;
  activeTextureUint = INVALID_VALUE;
  readFramebuffer = INVALID_VALUE;
  drawFramebuffer = INVALID_VALUE;
  program = INVALID_VALUE;
  vertexArray = INVALID_VALUE;
  srcColorBlendFactor = INVALID_VALUE;
  dstColorBlendFactor = INVALID_VALUE;
  srcAlphaBlendFactor = INVALID_VALUE;
  dstAlphaBlendFactor = INVALID_VALUE;
  colorBlendOp = INVALID_VALUE;
  alphaBlendOp = INVALID_VALUE;
  colorWriteMask = 0xF;
}

void GLGPU::enableCapability(unsigned capability, bool enabled) {
  auto result = capabilities.find(capability);
  if (result != capabilities.end() && result->second == enabled) {
    return;
  }
  auto gl = interface->functions();
  if (enabled) {
    gl->enable(capability);
  } else {
    gl->disable(capability);
  }
  capabilities[capability] = enabled;
}

void GLGPU::setScissorRect(int x, int y, int width, int height) {
  if (scissorRect[0] == x && scissorRect[1] == y && scissorRect[2] == width &&
      scissorRect[3] == height) {
    return;
  }
  auto gl = interface->functions();
  gl->scissor(x, y, width, height);
  scissorRect[0] = x;
  scissorRect[1] = y;
  scissorRect[2] = width;
  scissorRect[3] = height;
}

void GLGPU::setViewport(int x, int y, int width, int height) {
  if (viewport[0] == x && viewport[1] == y && viewport[2] == width && viewport[3] == height) {
    return;
  }
  auto gl = interface->functions();
  gl->viewport(x, y, width, height);
  viewport[0] = x;
  viewport[1] = y;
  viewport[2] = width;
  viewport[3] = height;
}

void GLGPU::setClearColor(Color color) {
  if (clearColor.has_value() && *clearColor == color) {
    return;
  }
  auto gl = interface->functions();
  gl->clearColor(color.red, color.green, color.blue, color.alpha);
  clearColor = color;
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

void GLGPU::useProgram(unsigned programID) {
  if (program == programID) {
    return;
  }
  auto gl = interface->functions();
  gl->useProgram(programID);
  program = programID;
}

void GLGPU::bindVertexArray(unsigned vao) {
  DEBUG_ASSERT(interface->caps()->vertexArrayObjectSupport);
  if (vertexArray == vao) {
    return;
  }
  auto gl = interface->functions();
  gl->bindVertexArray(vao);
  vertexArray = vao;
}

void GLGPU::setBlendFunc(unsigned srcColorFactor, unsigned dstColorFactor, unsigned srcAlphaFactor,
                         unsigned dstAlphaFactor) {
  if (srcColorFactor == srcColorBlendFactor && dstColorFactor == dstColorBlendFactor &&
      srcAlphaFactor == srcAlphaBlendFactor && dstAlphaFactor == dstAlphaBlendFactor) {
    return;
  }
  auto gl = interface->functions();
  gl->blendFuncSeparate(srcColorFactor, dstColorFactor, srcAlphaFactor, dstAlphaFactor);
  srcColorBlendFactor = srcColorFactor;
  dstColorBlendFactor = dstColorFactor;
  srcAlphaBlendFactor = srcAlphaFactor;
  dstAlphaBlendFactor = dstAlphaFactor;
}

void GLGPU::setBlendEquation(unsigned colorOp, unsigned alphaOp) {
  if (colorOp == colorBlendOp && alphaOp == alphaBlendOp) {
    return;
  }
  auto gl = interface->functions();
  gl->blendEquationSeparate(colorOp, alphaOp);
  colorBlendOp = colorOp;
  alphaBlendOp = alphaOp;
}

void GLGPU::setColorMask(uint32_t colorMask) {
  if (colorMask == colorWriteMask) {
    return;
  }
  auto gl = interface->functions();
  auto red = (colorMask & ColorWriteMask::RED) != 0;
  auto green = (colorMask & ColorWriteMask::GREEN) != 0;
  auto blue = (colorMask & ColorWriteMask::BLUE) != 0;
  auto alpha = (colorMask & ColorWriteMask::ALPHA) != 0;
  gl->colorMask(red, green, blue, alpha);
  colorWriteMask = colorMask;
}

}  // namespace tgfx

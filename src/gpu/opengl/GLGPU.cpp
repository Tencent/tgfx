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
#include "gpu/opengl/GLTextureBuffer.h"
#if defined(__EMSCRIPTEN__)
#include "gpu/opengl/webgl/WebGLBuffer.h"
#endif
#include "gpu/opengl/GLCommandEncoder.h"
#include "gpu/opengl/GLDepthStencilTexture.h"
#include "gpu/opengl/GLExternalTexture.h"
#include "gpu/opengl/GLMultisampleTexture.h"
#include "gpu/opengl/GLRenderPipeline.h"
#include "gpu/opengl/GLSampler.h"
#include "gpu/opengl/GLSemaphore.h"
#include "gpu/opengl/GLShaderModule.h"
#include "gpu/opengl/GLUtil.h"

namespace tgfx {
GLGPU::GLGPU(std::shared_ptr<GLInterface> glInterface)
    : _state(std::make_shared<GLState>(glInterface)), interface(std::move(glInterface)) {
  commandQueue = std::make_unique<GLCommandQueue>(this);
}

GLGPU::~GLGPU() {
  // The Device owner must call releaseAll() before deleting this GLGPU, otherwise, GPU resources
  // may leak.
  DEBUG_ASSERT(returnQueue == nullptr);
  DEBUG_ASSERT(resources.empty());
}

std::shared_ptr<GPUBuffer> GLGPU::createBuffer(size_t size, uint32_t usage) {
  if (size == 0) {
    return nullptr;
  }
  unsigned target = GLBuffer::GetTarget(usage);
  if (target == 0) {
    LOGE("GLGPU::createBuffer() invalid buffer usage!");
    return nullptr;
  }
  auto caps = interface->caps();
  if (!caps->pboSupport && usage & GPUBufferUsage::READBACK) {
    if (usage != GPUBufferUsage::READBACK) {
      LOGE(
          "GLGPU::createBuffer() READBACK usage can't be combined with other usages when PBO is "
          "not supported!");
      return nullptr;
    }
    return makeResource<GLTextureBuffer>(interface, _state, size);
  }

  auto gl = interface->functions();
  unsigned bufferID = 0;
  gl->genBuffers(1, &bufferID);
  if (bufferID == 0) {
    return nullptr;
  }
  gl->bindBuffer(target, bufferID);
  unsigned glUsage = usage & GPUBufferUsage::READBACK ? GL_STREAM_READ : GL_STATIC_DRAW;
  gl->bufferData(target, static_cast<GLsizeiptr>(size), nullptr, glUsage);
#if defined(__EMSCRIPTEN__)
  return makeResource<WebGLBuffer>(interface, bufferID, size, usage);
#else
  return makeResource<GLBuffer>(interface, bufferID, size, usage);
#endif
}

std::shared_ptr<GPUTexture> GLGPU::createTexture(const GPUTextureDescriptor& descriptor) {
  if (descriptor.width <= 0 || descriptor.height <= 0 ||
      descriptor.format == PixelFormat::Unknown || descriptor.mipLevelCount < 1 ||
      descriptor.sampleCount < 1 || descriptor.usage == 0) {
    LOGE("GLGPU::createTexture() invalid texture descriptor!");
    return nullptr;
  }
  if (descriptor.sampleCount > 1) {
    return GLMultisampleTexture::MakeFrom(this, descriptor);
  }
  if (descriptor.format == PixelFormat::DEPTH24_STENCIL8) {
    return GLDepthStencilTexture::MakeFrom(this, descriptor);
  }
  if (descriptor.usage & GPUTextureUsage::RENDER_ATTACHMENT &&
      !isFormatRenderable(descriptor.format)) {
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
  auto texture = makeResource<GLTexture>(descriptor, target, textureID);
  _state->bindTexture(texture.get());
  auto& textureFormat = interface->caps()->getTextureFormat(descriptor.format);
  bool success = true;
  // Texture memory must be allocated first on the web platform then can write pixels.
  for (int level = 0; level < descriptor.mipLevelCount && success; level++) {
    auto twoToTheMipLevel = 1 << level;
    auto currentWidth = std::max(1, descriptor.width / twoToTheMipLevel);
    auto currentHeight = std::max(1, descriptor.height / twoToTheMipLevel);
    gl->texImage2D(target, level, static_cast<int>(textureFormat.internalFormatTexImage),
                   currentWidth, currentHeight, 0, textureFormat.externalFormat,
                   textureFormat.externalType, nullptr);
    success = CheckGLError(gl);
  }
  if (!success) {
    return nullptr;
  }
  if (descriptor.usage & GPUTextureUsage::RENDER_ATTACHMENT && !texture->checkFrameBuffer(this)) {
    return nullptr;
  }
  return texture;
}

std::shared_ptr<GPUTexture> GLGPU::importBackendTexture(const BackendTexture& backendTexture,
                                                        uint32_t usage, bool adopted) {
  GLTextureInfo textureInfo = {};
  if (!backendTexture.getGLTextureInfo(&textureInfo)) {
    return nullptr;
  }
  auto format = backendTexture.format();
  if (usage & GPUTextureUsage::RENDER_ATTACHMENT && !isFormatRenderable(format)) {
    LOGE(
        "GLGPU::importExternalTexture() format is not renderable but RENDER_ATTACHMENT usage is "
        "set!");
    return nullptr;
  }
  GPUTextureDescriptor descriptor = {
      backendTexture.width(), backendTexture.height(), format, false, 1, usage};
  std::shared_ptr<GLTexture> texture = nullptr;
  if (adopted) {
    texture = makeResource<GLTexture>(descriptor, textureInfo.target, textureInfo.id);
  } else {
    texture = makeResource<GLExternalTexture>(descriptor, textureInfo.target, textureInfo.id);
  }
  if (usage & GPUTextureUsage::RENDER_ATTACHMENT && !texture->checkFrameBuffer(this)) {
    return nullptr;
  }
  return texture;
}

std::shared_ptr<GPUTexture> GLGPU::importBackendRenderTarget(
    const BackendRenderTarget& renderTarget) {
  GLFrameBufferInfo frameBufferInfo = {};
  if (!renderTarget.getGLFramebufferInfo(&frameBufferInfo)) {
    return nullptr;
  }
  auto format = renderTarget.format();
  if (!isFormatRenderable(format)) {
    return nullptr;
  }
  GPUTextureDescriptor descriptor = {renderTarget.width(),
                                     renderTarget.height(),
                                     format,
                                     false,
                                     1,
                                     GPUTextureUsage::RENDER_ATTACHMENT};
  return makeResource<GLExternalTexture>(descriptor, static_cast<unsigned>(GL_TEXTURE_2D), 0u,
                                         frameBufferInfo.id);
}

std::shared_ptr<Semaphore> GLGPU::importBackendSemaphore(const BackendSemaphore& semaphore) {
  GLSyncInfo glSyncInfo = {};
  if (!semaphore.getGLSync(&glSyncInfo)) {
    return nullptr;
  }
  return makeResource<GLSemaphore>(glSyncInfo.sync);
}

BackendSemaphore GLGPU::stealBackendSemaphore(std::shared_ptr<Semaphore> semaphore) {
  if (semaphore == nullptr || semaphore.use_count() > 1) {
    return {};
  }
  auto backendSemaphore = semaphore->getBackendSemaphore();
  static_cast<GLSemaphore*>(semaphore.get())->_glSync = nullptr;
  return backendSemaphore;
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

std::shared_ptr<GPUSampler> GLGPU::createSampler(const GPUSamplerDescriptor& descriptor) {
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
  return std::make_shared<GLSampler>(ToGLWrap(descriptor.addressModeX),
                                     ToGLWrap(descriptor.addressModeY), minFilter, magFilter);
}

std::shared_ptr<ShaderModule> GLGPU::createShaderModule(const ShaderModuleDescriptor& descriptor) {
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
  const char* files[] = {code.c_str()};
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
  return makeResource<GLShaderModule>(shader);
}

std::shared_ptr<RenderPipeline> GLGPU::createRenderPipeline(
    const RenderPipelineDescriptor& descriptor) {
  auto vertexModule = static_cast<GLShaderModule*>(descriptor.vertex.module.get());
  auto fragmentModule = static_cast<GLShaderModule*>(descriptor.fragment.module.get());
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
  auto pipeline = makeResource<GLRenderPipeline>(programID);
  if (!pipeline->setPipelineDescriptor(this, descriptor)) {
    return nullptr;
  }
  return pipeline;
}

std::shared_ptr<CommandEncoder> GLGPU::createCommandEncoder() {
  processUnreferencedResources();
  return std::make_shared<GLCommandEncoder>(this);
}

void GLGPU::processUnreferencedResources() {
  DEBUG_ASSERT(returnQueue != nullptr);
  while (auto resource = static_cast<GLResource*>(returnQueue->dequeue())) {
    resources.erase(resource->cachedPosition);
    resource->onRelease(this);
    delete resource;
  }
}

void GLGPU::releaseAll(bool releaseGPU) {
  if (releaseGPU) {
    for (auto& resource : resources) {
      resource->onRelease(this);
    }
  }
  resources.clear();
  returnQueue = nullptr;
}

std::shared_ptr<GLResource> GLGPU::addResource(GLResource* resource) {
  DEBUG_ASSERT(resource != nullptr);
  processUnreferencedResources();
  resources.push_back(resource);
  resource->cachedPosition = --resources.end();
  return std::static_pointer_cast<GLResource>(returnQueue->makeShared(resource));
}

}  // namespace tgfx

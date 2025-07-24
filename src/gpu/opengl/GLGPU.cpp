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
#include "GLUtil.h"
#include "core/utils/PixelFormatUtil.h"
#include "gpu/opengl/GLRenderTarget.h"
#include "gpu/opengl/GLSemaphore.h"

namespace tgfx {
static int FilterToGLMagFilter(FilterMode filterMode) {
  switch (filterMode) {
    case FilterMode::Nearest:
      return GL_NEAREST;
    case FilterMode::Linear:
      return GL_LINEAR;
  }
}

static int FilterToGLMinFilter(FilterMode filterMode, MipmapMode mipmapMode) {
  switch (mipmapMode) {
    case MipmapMode::None:
      return FilterToGLMagFilter(filterMode);
    case MipmapMode::Nearest:
      switch (filterMode) {
        case FilterMode::Nearest:
          return GL_NEAREST_MIPMAP_NEAREST;
        case FilterMode::Linear:
          return GL_LINEAR_MIPMAP_NEAREST;
      }
    case MipmapMode::Linear:
      switch (filterMode) {
        case FilterMode::Nearest:
          return GL_NEAREST_MIPMAP_LINEAR;
        case FilterMode::Linear:
          return GL_LINEAR_MIPMAP_LINEAR;
      }
  }
}

static int GetGLWrap(unsigned target, SamplerState::WrapMode wrapMode) {
  if (target == GL_TEXTURE_RECTANGLE) {
    if (wrapMode == SamplerState::WrapMode::ClampToBorder) {
      return GL_CLAMP_TO_BORDER;
    }
    return GL_CLAMP_TO_EDGE;
  }
  switch (wrapMode) {
    case SamplerState::WrapMode::Clamp:
      return GL_CLAMP_TO_EDGE;
    case SamplerState::WrapMode::Repeat:
      return GL_REPEAT;
    case SamplerState::WrapMode::MirrorRepeat:
      return GL_MIRRORED_REPEAT;
    case SamplerState::WrapMode::ClampToBorder:
      return GL_CLAMP_TO_BORDER;
  }
}

void GLGPU::bindTexture(int unitIndex, const TextureSampler* sampler, SamplerState samplerState) {
  if (sampler == nullptr) {
    return;
  }
  auto gl = GLFunctions::Get(context);
  auto glSampler = static_cast<const GLTextureSampler*>(sampler);
  auto target = glSampler->target();
  gl->activeTexture(static_cast<unsigned>(GL_TEXTURE0 + unitIndex));
  gl->bindTexture(target, glSampler->id());
  gl->texParameteri(target, GL_TEXTURE_WRAP_S, GetGLWrap(target, samplerState.wrapModeX));
  gl->texParameteri(target, GL_TEXTURE_WRAP_T, GetGLWrap(target, samplerState.wrapModeY));
  if (samplerState.mipmapped() && (!context->caps()->mipmapSupport || !glSampler->hasMipmaps())) {
    samplerState.mipmapMode = MipmapMode::None;
  }
  gl->texParameteri(target, GL_TEXTURE_MIN_FILTER,
                    FilterToGLMinFilter(samplerState.filterMode, samplerState.mipmapMode));
  gl->texParameteri(target, GL_TEXTURE_MAG_FILTER, FilterToGLMagFilter(samplerState.filterMode));
}

void GLGPU::copyRenderTargetToTexture(const RenderTarget* renderTarget, Texture* texture, int srcX,
                                      int srcY) {
  DEBUG_ASSERT(renderTarget != nullptr);
  DEBUG_ASSERT(texture != nullptr);
  auto width = std::min(texture->width(), renderTarget->width() - srcX);
  auto height = std::min(texture->height(), renderTarget->height() - srcY);
  auto gl = GLFunctions::Get(context);
  auto glRenderTarget = static_cast<const GLRenderTarget*>(renderTarget);
  gl->bindFramebuffer(GL_FRAMEBUFFER, glRenderTarget->readFrameBufferID());
  auto glSampler = static_cast<const GLTextureSampler*>(texture->getSampler());
  auto target = glSampler->target();
  gl->bindTexture(target, glSampler->id());
  gl->copyTexSubImage2D(target, 0, 0, 0, srcX, srcY, width, height);
}

std::shared_ptr<Semaphore> GLGPU::insertSemaphore() {
  if (!context->caps()->semaphoreSupport) {
    return nullptr;
  }
  auto gl = GLFunctions::Get(context);
  auto* glSync = gl->fenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
  if (glSync) {
    // If we inserted semaphores during the flush, we need to call glFlush.
    gl->flush();
    return Resource::AddToCache(context, new GLSemaphore(glSync));
  }
  return nullptr;
}

void GLGPU::waitSemaphore(const Semaphore* semaphore) {
  DEBUG_ASSERT(semaphore != nullptr);
  auto glSync = static_cast<const GLSemaphore*>(semaphore)->glSync();
  if (glSync != nullptr) {
    auto gl = GLFunctions::Get(context);
    gl->waitSync(glSync, 0, GL_TIMEOUT_IGNORED);
  }
}

bool GLGPU::submitToGPU(bool syncCpu) {
  auto gl = GLFunctions::Get(context);
  if (syncCpu) {
    gl->finish();
  } else {
    gl->flush();
  }
  return true;
}
}  // namespace tgfx

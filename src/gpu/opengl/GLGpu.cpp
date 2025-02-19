/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "GLGpu.h"
#include "GLUtil.h"
#include "core/utils/PixelFormatUtil.h"
#include "gpu/opengl/GLRenderTarget.h"
#include "gpu/opengl/GLSemaphore.h"

namespace tgfx {
std::unique_ptr<Gpu> GLGpu::Make(Context* context) {
  return std::unique_ptr<GLGpu>(new GLGpu(context));
}

std::shared_ptr<RenderPass> GLGpu::getRenderPass() {
  if (renderPass == nullptr) {
    renderPass = std::make_shared<GLRenderPass>(context);
  }
  return renderPass;
}

std::unique_ptr<TextureSampler> GLGpu::createSampler(int width, int height, PixelFormat format,
                                                     int mipLevelCount) {
  // Texture memory must be allocated first on the web platform then can write pixels.
  DEBUG_ASSERT(mipLevelCount > 0);
  // Clear the previously generated GLError, causing the subsequent CheckGLError to return an
  // incorrect result.
  CheckGLError(context);
  auto gl = GLFunctions::Get(context);
  auto sampler = std::make_unique<GLSampler>();
  gl->genTextures(1, &(sampler->id));
  if (sampler->id == 0) {
    return nullptr;
  }
  sampler->target = GL_TEXTURE_2D;
  sampler->format = format;
  sampler->maxMipmapLevel = mipLevelCount - 1;
  gl->bindTexture(sampler->target, sampler->id);
  gl->texParameteri(sampler->target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  gl->texParameteri(sampler->target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  gl->texParameteri(sampler->target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  gl->texParameteri(sampler->target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  const auto& textureFormat = GLCaps::Get(context)->getTextureFormat(format);
  bool success = true;
  for (int level = 0; level < mipLevelCount && success; level++) {
    const int twoToTheMipLevel = 1 << level;
    const int currentWidth = std::max(1, width / twoToTheMipLevel);
    const int currentHeight = std::max(1, height / twoToTheMipLevel);
    gl->texImage2D(sampler->target, level, static_cast<int>(textureFormat.internalFormatTexImage),
                   currentWidth, currentHeight, 0, textureFormat.externalFormat, GL_UNSIGNED_BYTE,
                   nullptr);
    success = CheckGLError(context);
  }
  if (!success) {
    gl->deleteTextures(1, &(sampler->id));
    return nullptr;
  }
  return sampler;
}

void GLGpu::deleteSampler(TextureSampler* sampler) {
  auto glSampler = static_cast<GLSampler*>(sampler);
  if (glSampler == nullptr || glSampler->id == 0) {
    return;
  }
  GLFunctions::Get(context)->deleteTextures(1, &glSampler->id);
  glSampler->id = 0;
}

void GLGpu::writePixels(const TextureSampler* sampler, Rect rect, const void* pixels,
                        size_t rowBytes) {
  if (sampler == nullptr) {
    return;
  }
  auto gl = GLFunctions::Get(context);
  // https://skia-review.googlesource.com/c/skia/+/571418
  // HUAWEI nova9 pro(Adreno 642L), iqoo neo5(Adreno 650), Redmi K30pro(Adreno 650),
  // Xiaomi 8(Adreno 630), glaxy s9(Adreno 630)
  gl->flush();
  auto caps = GLCaps::Get(context);
  auto glSampler = static_cast<const GLSampler*>(sampler);
  gl->bindTexture(glSampler->target, glSampler->id);
  const auto& format = caps->getTextureFormat(sampler->format);
  auto bytesPerPixel = PixelFormatBytesPerPixel(sampler->format);
  gl->pixelStorei(GL_UNPACK_ALIGNMENT, static_cast<int>(bytesPerPixel));
  int x = static_cast<int>(rect.x());
  int y = static_cast<int>(rect.y());
  int width = static_cast<int>(rect.width());
  int height = static_cast<int>(rect.height());
  if (caps->unpackRowLengthSupport) {
    // the number of pixels, not bytes
    gl->pixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<int>(rowBytes / bytesPerPixel));
    gl->texSubImage2D(glSampler->target, 0, x, y, width, height, format.externalFormat,
                      GL_UNSIGNED_BYTE, pixels);
    gl->pixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  } else {
    if (static_cast<size_t>(width) * bytesPerPixel == rowBytes) {
      gl->texSubImage2D(glSampler->target, 0, x, y, width, height, format.externalFormat,
                        GL_UNSIGNED_BYTE, pixels);
    } else {
      auto data = reinterpret_cast<const uint8_t*>(pixels);
      for (int row = 0; row < height; ++row) {
        gl->texSubImage2D(glSampler->target, 0, x, y + row, width, 1, format.externalFormat,
                          GL_UNSIGNED_BYTE, data + (static_cast<size_t>(row) * rowBytes));
      }
    }
  }
  if (glSampler->hasMipmaps() && glSampler->target == GL_TEXTURE_2D) {
    gl->generateMipmap(glSampler->target);
  }
}

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
    } else {
      return GL_CLAMP_TO_EDGE;
    }
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

void GLGpu::bindTexture(int unitIndex, const TextureSampler* sampler, SamplerState samplerState) {
  if (sampler == nullptr) {
    return;
  }
  auto glSampler = static_cast<const GLSampler*>(sampler);
  auto gl = GLFunctions::Get(context);
  gl->activeTexture(static_cast<unsigned>(GL_TEXTURE0 + unitIndex));
  gl->bindTexture(glSampler->target, glSampler->id);
  gl->texParameteri(glSampler->target, GL_TEXTURE_WRAP_S,
                    GetGLWrap(glSampler->target, samplerState.wrapModeX));
  gl->texParameteri(glSampler->target, GL_TEXTURE_WRAP_T,
                    GetGLWrap(glSampler->target, samplerState.wrapModeY));
  if (samplerState.mipmapped() && (!context->caps()->mipmapSupport || !glSampler->hasMipmaps())) {
    samplerState.mipmapMode = MipmapMode::None;
  }
  gl->texParameteri(glSampler->target, GL_TEXTURE_MIN_FILTER,
                    FilterToGLMinFilter(samplerState.filterMode, samplerState.mipmapMode));
  gl->texParameteri(glSampler->target, GL_TEXTURE_MAG_FILTER,
                    FilterToGLMagFilter(samplerState.filterMode));
}

void GLGpu::copyRenderTargetToTexture(const RenderTarget* renderTarget, Texture* texture, int srcX,
                                      int srcY) {
  DEBUG_ASSERT(srcX >= 0 && srcX + texture->width() <= renderTarget->width());
  DEBUG_ASSERT(srcY >= 0 && srcY + texture->height() <= renderTarget->height());
  auto gl = GLFunctions::Get(context);
  auto glRenderTarget = static_cast<const GLRenderTarget*>(renderTarget);
  gl->bindFramebuffer(GL_FRAMEBUFFER, glRenderTarget->getFrameBufferID(false));
  auto glSampler = static_cast<const GLSampler*>(texture->getSampler());
  gl->bindTexture(glSampler->target, glSampler->id);
  gl->copyTexSubImage2D(glSampler->target, 0, 0, 0, srcX, srcY, texture->width(),
                        texture->height());
  if (glSampler->hasMipmaps() && glSampler->target == GL_TEXTURE_2D) {
    gl->generateMipmap(glSampler->target);
  }
}

void GLGpu::resolveRenderTarget(RenderTarget* renderTarget, const Rect& bounds) {
  if (renderTarget->sampleCount() <= 1) {
    return;
  }
  auto gl = GLFunctions::Get(context);
  auto caps = GLCaps::Get(context);
  if (!caps->usesMSAARenderBuffers()) {
    return;
  }
  auto x = static_cast<int>(bounds.x());
  auto y = static_cast<int>(bounds.y());
  auto width = static_cast<int>(bounds.width());
  auto height = static_cast<int>(bounds.height());
  DEBUG_ASSERT(bounds.x() == static_cast<float>(x) && bounds.y() == static_cast<float>(y) &&
               bounds.width() == static_cast<float>(width) &&
               bounds.height() == static_cast<float>(height));
  auto glRT = static_cast<GLRenderTarget*>(renderTarget);
  gl->bindFramebuffer(GL_READ_FRAMEBUFFER, glRT->getFrameBufferID(true));
  gl->bindFramebuffer(GL_DRAW_FRAMEBUFFER, glRT->getFrameBufferID(false));
  if (caps->msFBOType == MSFBOType::ES_Apple) {
    // Apple's extension uses the scissor as the blit bounds.
    gl->enable(GL_SCISSOR_TEST);
    gl->scissor(x, y, width, height);
    gl->resolveMultisampleFramebuffer();
    gl->disable(GL_SCISSOR_TEST);
  } else {
    // BlitFrameBuffer respects the scissor, so disable it.
    gl->disable(GL_SCISSOR_TEST);
    gl->blitFramebuffer(x, y, width, height, x, y, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
  }
}

void GLGpu::regenerateMipmapLevels(const TextureSampler* sampler) {
  auto gl = GLFunctions::Get(context);
  auto glSampler = static_cast<const GLSampler*>(sampler);
  if (!glSampler->hasMipmaps() || glSampler->target != GL_TEXTURE_2D) {
    return;
  }
  gl->bindTexture(glSampler->target, glSampler->id);
  gl->generateMipmap(glSampler->target);
}

bool GLGpu::insertSemaphore(Semaphore* semaphore) {
  if (semaphore == nullptr) {
    return false;
  }
  auto gl = GLFunctions::Get(context);
  auto* sync = gl->fenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
  if (sync) {
    static_cast<GLSemaphore*>(semaphore)->glSync = sync;
    // If we inserted semaphores during the flush, we need to call glFlush.
    gl->flush();
    return true;
  }
  return false;
}

bool GLGpu::waitSemaphore(const Semaphore* semaphore) {
  auto glSync = static_cast<const GLSemaphore*>(semaphore)->glSync;
  if (glSync == nullptr) {
    return false;
  }
  auto gl = GLFunctions::Get(context);
  gl->waitSync(glSync, 0, GL_TIMEOUT_IGNORED);
  gl->deleteSync(glSync);
  return true;
}

bool GLGpu::submitToGpu(bool syncCpu) {
  auto gl = GLFunctions::Get(context);
  if (syncCpu) {
    gl->finish();
  } else {
    gl->flush();
  }
  return true;
}
}  // namespace tgfx

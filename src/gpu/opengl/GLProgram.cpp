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

#include "GLProgram.h"
#include "gpu/opengl/GLGPU.h"
#include "gpu/opengl/GLTextureSampler.h"
#include "gpu/opengl/GLUtil.h"

namespace tgfx {
GLProgram::GLProgram(unsigned programID, std::unique_ptr<GLUniformBuffer> uniformBuffer,
                     std::vector<Attribute> attributes, int vertexStride)
    : programId(programID), uniformBuffer(std::move(uniformBuffer)),
      attributes(std::move(attributes)), _vertexStride(vertexStride) {
}

void GLProgram::onReleaseGPU() {
  if (programId) {
    auto gl = GLFunctions::Get(context);
    gl->deleteProgram(programId);
  }
}

void GLProgram::updateUniformsAndTextureBindings(const RenderTarget* renderTarget,
                                                 const Pipeline* pipeline) {
  setRenderTargetState(renderTarget);
  pipeline->getUniforms(uniformBuffer.get());
  uniformBuffer->uploadToGPU(context);
  auto samplers = pipeline->getSamplers();
  int textureUnit = 0;
  for (auto& info : samplers) {
    bindTexture(textureUnit++, info.sampler, info.state);
  }
}

static std::array<float, 4> GetRTAdjustArray(int width, int height, bool flipY) {
  std::array<float, 4> result = {};
  result[0] = 2.f / static_cast<float>(width);
  result[2] = 2.f / static_cast<float>(height);
  result[1] = -1.f;
  result[3] = -1.f;
  if (flipY) {
    result[2] = -result[2];
    result[3] = -result[3];
  }
  return result;
}

void GLProgram::setRenderTargetState(const RenderTarget* renderTarget) {
  int width = renderTarget->width();
  int height = renderTarget->height();
  auto origin = renderTarget->origin();
  if (renderTargetState.width == width && renderTargetState.height == height &&
      renderTargetState.origin == origin) {
    return;
  }
  renderTargetState.width = width;
  renderTargetState.height = height;
  renderTargetState.origin = origin;
  auto v = GetRTAdjustArray(width, height, origin == ImageOrigin::BottomLeft);
  uniformBuffer->setData(RTAdjustName, v);
}

static int FilterToGLMagFilter(FilterMode filterMode) {
  switch (filterMode) {
    case FilterMode::Nearest:
      return GL_NEAREST;
    case FilterMode::Linear:
      return GL_LINEAR;
  }
  return 0;
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
  return 0;
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
  return 0;
}

void GLProgram::bindTexture(int unitIndex, const TextureSampler* sampler,
                            SamplerState samplerState) {
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
}  // namespace tgfx

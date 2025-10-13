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

#pragma once

#include <memory>
#include <unordered_map>
#include "gpu/RenderPipeline.h"
#include "gpu/opengl/GLBuffer.h"
#include "gpu/opengl/GLResource.h"
#include "gpu/opengl/GLState.h"
#include "gpu/opengl/GLTexture.h"

namespace tgfx {
struct GLAttribute {
  int location = 0;
  int count = 0;
  unsigned type = 0;
  bool normalized = false;
  size_t offset = 0;
};

struct GLUniform {
  UniformFormat format = UniformFormat::Float;
  int location = -1;
  size_t offset = 0;
};

/**
 * GLRenderPipeline is the OpenGL implementation of the RenderPipeline interface. It encapsulates
 * an OpenGL shader program along with its associated state, such as vertex attributes and blending
 * settings.
 */
class GLRenderPipeline : public RenderPipeline, public GLResource {
 public:
  explicit GLRenderPipeline(unsigned programID);

  /**
   * Binds the shader program so that it is used in subsequent draw calls.
   */
  void activate(GLGPU* gpu, bool depthReadOnly, bool stencilReadOnly, unsigned stencilReference);

  /**
   * Sets a uniform buffer to a specified binding index.
   */
  void setUniformBuffer(GLGPU* gpu, unsigned binding, GLBuffer* buffer, size_t offset, size_t size);

  /**
   * Sets a texture and its sampler state to a specified binding index.
   */
  void setTexture(GLGPU* gpu, unsigned binding, GLTexture* texture, GLSampler* sampler);

  /**
   * Binds the vertex buffer to be used in subsequent draw calls. The vertexOffset is the offset
   * into the buffer where the vertex data begins.
   */
  void setVertexBuffer(GLGPU* gpu, GLBuffer* vertexBuffer, size_t vertexOffset);

  /**
   * Sets the stencil reference value for stencil testing.
   */
  void setStencilReference(GLGPU* gpu, unsigned reference);

 protected:
  void onRelease(GLGPU* gpu) override;

 private:
  unsigned programID = 0;
  unsigned vertexArray = 0;
  std::vector<GLAttribute> attributes = {};
  size_t vertexStride = 0;
  // only used if UBOs are not supported.
  std::unordered_map<unsigned, std::vector<GLUniform>> uniformBlocks = {};
  std::unordered_map<unsigned, unsigned> textureUnits = {};
  uint32_t colorWriteMask = ColorWriteMask::All;
  std::unique_ptr<GLStencilState> stencilState = nullptr;
  std::unique_ptr<GLDepthState> depthState = nullptr;
  std::unique_ptr<GLBlendState> blendState = nullptr;

  bool setPipelineDescriptor(GLGPU* gpu, const RenderPipelineDescriptor& descriptor);

  friend class GLGPU;
};
}  // namespace tgfx

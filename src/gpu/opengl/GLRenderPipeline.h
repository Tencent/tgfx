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
#include "gpu/GPURenderPipeline.h"
#include "gpu/opengl/GLBuffer.h"
#include "gpu/opengl/GLTexture.h"

namespace tgfx {
class GLGPU;

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

struct GLUniformBlock {
  unsigned ubo = 0;                      // OpenGL UBO handle, 0 if UBOs are not supported.
  std::vector<GLUniform> uniforms = {};  // only used if UBOs are not supported.
};

/**
 * GLRenderPipeline is the OpenGL implementation of the GPURenderPipeline interface. It encapsulates
 * an OpenGL shader program along with its associated state, such as vertex attributes and blending
 * settings.
 */
class GLRenderPipeline : public GPURenderPipeline {
 public:
  explicit GLRenderPipeline(unsigned programID);

  /**
   * Binds the shader program so that it is used in subsequent draw calls.
   */
  void activate(GLGPU* gpu);

  /**
   * Sets the uniform data to a specified binding index.
   */
  void setUniformBytes(GLGPU* gpu, unsigned binding, const void* data, size_t size);

  /**
   * Sets a texture and its sampler state to a specified binding index.
   */
  void setTexture(GLGPU* gpu, unsigned binding, GLTexture* texture, GLSampler* sampler);

  /**
   * Binds the vertex buffer to be used in subsequent draw calls. The vertexOffset is the offset
   * into the buffer where the vertex data begins.
   */
  void setVertexBuffer(GLGPU* gpu, GLBuffer* vertexBuffer, size_t vertexOffset);

  void release(GPU* gpu) override;

 private:
  unsigned programID = 0;
  unsigned vertexArray = 0;
  std::vector<GLAttribute> attributes = {};
  size_t vertexStride = 0;
  std::unordered_map<unsigned, GLUniformBlock> uniformBlocks = {};
  std::unordered_map<unsigned, unsigned> textureUnits = {};
  PipelineColorAttachment colorAttachment = {};

  bool setPipelineDescriptor(GLGPU* gpu, const GPURenderPipelineDescriptor& descriptor);

  friend class GLGPU;
};
}  // namespace tgfx

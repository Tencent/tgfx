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

#include "gpu/Attribute.h"
#include "gpu/Blend.h"
#include "gpu/GPURenderPipeline.h"
#include "gpu/Uniform.h"
#include "gpu/opengl/GLBuffer.h"

namespace tgfx {
class GLGPU;

/**
 * GLRenderPipeline is the OpenGL implementation of the GPURenderPipeline interface. It encapsulates
 * an OpenGL shader program along with its associated state, such as vertex attributes and blending
 * settings.
 */
class GLRenderPipeline : public GPURenderPipeline {
 public:
  GLRenderPipeline(unsigned programID, std::vector<Uniform> uniforms,
                   std::vector<Attribute> attribs, std::unique_ptr<BlendFormula> blendFormula);

  /**
   * Binds the shader program so that it is used in subsequent draw calls.
   */
  void activate(GLGPU* gpu);

  /**
   * Sets the uniform data to be used in subsequent draw calls.
   */
  void setUniformBytes(GLGPU* gpu, const void* data, size_t size);

  /**
   * Binds the vertex buffer to be used in subsequent draw calls. The vertexOffset is the offset
   * into the buffer where the vertex data begins.
   */
  void setVertexBuffer(GLGPU* gpu, GPUBuffer* vertexBuffer, size_t vertexOffset);

  void release(GPU* gpu) override;

 private:
  unsigned programID = 0;
  unsigned vertexArray = 0;
  std::vector<Uniform> uniforms = {};
  std::vector<Attribute> attributes = {};
  std::vector<int> attributeLocations = {};
  std::vector<int> uniformLocations = {};
  int vertexStride = 0;
  std::unique_ptr<BlendFormula> blendFormula = nullptr;
};
}  // namespace tgfx

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

#include <optional>
#include "gpu/Program.h"
#include "gpu/ProgramInfo.h"
#include "gpu/opengl/GLBuffer.h"

namespace tgfx {
class GLProgram : public Program {
 public:
  GLProgram(unsigned programID, std::unique_ptr<UniformBuffer> uniformBuffer,
            std::vector<Attribute> attributes, std::unique_ptr<BlendFormula> blendFormula);

  /**
   * Binds the program so that it is used in subsequent draw calls.
   */
  void activate();

  /**
   * Sets the uniform data to be used in subsequent draw calls.
   */
  void setUniformBytes();

  void setUniformBuffer() const;

  /**
   * Binds the vertex buffer to be used in subsequent draw calls. The vertexOffset is the offset
   * into the buffer where the vertex data begins.
   */
  void setVertexBuffer(GPUBuffer* vertexBuffer, size_t vertexOffset);

  /**
   * Binds the index buffer to be used in subsequent draw calls.
   */
  void setIndexBuffer(GPUBuffer* indexBuffer);

 protected:
  void onReleaseGPU() override;

 private:
  unsigned programID = 0;
  unsigned vertexArray = 0;
  std::vector<Attribute> _attributes = {};
  std::vector<int> attributeLocations = {};
  std::vector<int> uniformLocations = {};
  int vertexStride = 0;
  std::unique_ptr<BlendFormula> blendFormula = nullptr;
  unsigned int vertexUBO = 0;
  unsigned int fragmentUBO = 0;
  unsigned int vertexUniformBlockIndex = 0;
  unsigned int fragmentUniformBlockIndex = 0;
};
}  // namespace tgfx

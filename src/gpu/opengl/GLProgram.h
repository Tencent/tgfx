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
#include "gpu/Pipeline.h"
#include "gpu/Program.h"
#include "gpu/SLType.h"
#include "gpu/opengl/GLUniformBuffer.h"

namespace tgfx {
class GLProgram : public Program {
 public:
  struct Attribute {
    SLType gpuType = SLType::Float;
    size_t offset = 0;
    int location = 0;
  };

  GLProgram(unsigned programID, std::unique_ptr<GLUniformBuffer> uniformBuffer,
            std::vector<Attribute> attributes, int vertexStride);

  /**
   * Gets the GL program ID for this program.
   */
  unsigned programID() const {
    return programId;
  }

  GLUniformBuffer* uniformBuffer() const {
    return _uniformBuffer.get();
  }

  int vertexStride() const {
    return _vertexStride;
  }

  const std::vector<Attribute>& vertexAttributes() const {
    return attributes;
  }

 protected:
  void onReleaseGPU() override;

 private:
  unsigned programId = 0;
  std::unique_ptr<GLUniformBuffer> _uniformBuffer = nullptr;
  std::vector<Attribute> attributes = {};
  int _vertexStride = 0;
};
}  // namespace tgfx

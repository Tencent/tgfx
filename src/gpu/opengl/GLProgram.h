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
#include "gpu/opengl/GLBuffer.h"

namespace tgfx {
class GLProgram : public Program {
 public:
  GLProgram(unsigned programID, std::unique_ptr<UniformBuffer> uniformBuffer,
            std::vector<Attribute> attributes);

  /**
   * Gets the GL program ID for this program.
   */
  unsigned programID() const {
    return _programID;
  }

  void setVertexBuffer(GLBuffer* buffer, size_t offset);

  void setUniformBytes(const void* data, size_t size);

 protected:
  void onReleaseGPU() override;

 private:
  unsigned _programID = 0;
  std::vector<int> uniformLocations = {};
  std::vector<Attribute> _attributes = {};
  std::vector<int> attributeLocations = {};
  int vertexStride = 0;
};
}  // namespace tgfx

/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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
#include <string>
#include <vector>
#include "Uniform.h"
#include "UniformBuffer.h"

namespace tgfx {
class UniformLayout final {
 public:
  UniformLayout(const std::vector<std::string>& uniformBlockNames,
                UniformBuffer* vertexUniformBuffer, UniformBuffer* fragmentUniformBuffer)
      : _uniformBlockNames(uniformBlockNames), _vertexUniformBuffer(vertexUniformBuffer),
        _fragmentUniformBuffer(fragmentUniformBuffer) {
  }

  std::vector<std::string>& uniformBlockNames() {
    return _uniformBlockNames;
  }

  const std::vector<Uniform> vertexUniforms() const {
    if (_vertexUniformBuffer == nullptr) {
      return {};
    } else {
      return _vertexUniformBuffer->uniforms();
    }
  }

  const std::vector<Uniform> fragmentUniforms() const {
    if (_fragmentUniformBuffer == nullptr) {
      return {};
    } else {
      return _fragmentUniformBuffer->uniforms();
    }
  }

 private:
  std::vector<std::string> _uniformBlockNames = {};
  UniformBuffer* _vertexUniformBuffer = nullptr;
  UniformBuffer* _fragmentUniformBuffer = nullptr;
};
}  // namespace tgfx

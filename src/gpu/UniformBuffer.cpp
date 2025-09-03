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

#include "UniformBuffer.h"
#include "core/utils/Log.h"

namespace tgfx {
UniformBuffer::UniformBuffer(const std::vector<Uniform>& vertexUniforms,
                             const std::vector<Uniform>& fragmentUniforms, bool uboSupport)
    : _uboSupport(uboSupport) {
  if (vertexUniforms.empty() && fragmentUniforms.empty()) {
    LOGE("UniformBuffer::UniformBuffer no uniforms");
    return;
  }

  _uniforms.reserve(vertexUniforms.size() + fragmentUniforms.size());
  _uniforms.insert(_uniforms.begin(), vertexUniforms.begin(), vertexUniforms.end());
  _uniforms.insert(_uniforms.end(), fragmentUniforms.begin(), fragmentUniforms.end());

  vertexUniformBufferLayout = std::make_unique<UniformBufferLayout>(_uboSupport);
  fragmentUniformBufferLayout = std::make_unique<UniformBufferLayout>(_uboSupport);

  for (const auto& uniform : vertexUniforms) {
    vertexUniformBufferLayout->add(uniform, 0);
  }
  _vertexUniformBufferSize = vertexUniformBufferLayout->totalSize();

  for (const auto& uniform : fragmentUniforms) {
    fragmentUniformBufferLayout->add(uniform, _vertexUniformBufferSize);
  }
  _fragmentUniformBufferSize = fragmentUniformBufferLayout->totalSize();

  if (const size_t totalSize = _vertexUniformBufferSize + _fragmentUniformBufferSize;
      totalSize > 0) {
    buffer.resize(totalSize);
  }
}

void UniformBuffer::setData(const std::string& name, const Matrix& matrix) {
  float values[6] = {};
  matrix.get6(values);

  if (_uboSupport) {
    const float data[] = {
        values[0], values[3], 0, 0, values[1], values[4], 0, 0, values[2], values[5], 1, 0,
    };
    onSetData(name, data, sizeof(data));
  } else {
    const float data[] = {values[0], values[3], 0,         values[1], values[4],
                          0,         values[2], values[5], 1};
    onSetData(name, data, sizeof(data));
  }
}

void UniformBuffer::onSetData(const std::string& name, const void* data, size_t size) {
  const auto& key = name + nameSuffix;
  auto* filed = vertexUniformBufferLayout->findField(key);
  if (filed == nullptr) {
    filed = fragmentUniformBufferLayout->findField(key);
  }
  if (filed == nullptr) {
    LOGE("UniformBuffer::onSetData() uniform '%s' not found!", name.c_str());
    return;
  }
  DEBUG_ASSERT(filed->size == size);

  memcpy(buffer.data() + filed->offset, data, size);
}
}  // namespace tgfx
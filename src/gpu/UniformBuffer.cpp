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
UniformBuffer::UniformBuffer(std::vector<Uniform> uniformList) : uniforms(std::move(uniformList)) {
  size_t index = 0;
  size_t offset = 0;
  offsets.push_back(offset);
  for (auto& uniform : uniforms) {
    uniformMap[uniform.name()] = index++;
    offset += uniform.size();
    offsets.push_back(offset);
  }
  if (offset > 0) {
    _buffer = new (std::nothrow) uint8_t[offset];
    if (_buffer == nullptr) {
      offsets.resize(1);
      LOGE("UniformBuffer::UniformBuffer() failed to allocate memory for uniform buffer!");
    }
  }
}

UniformBuffer::~UniformBuffer() {
  delete[] _buffer;
}

void UniformBuffer::setData(const std::string& name, const Matrix& matrix) {
  float values[6];
  matrix.get6(values);
  float data[] = {values[0], values[3], 0, values[1], values[4], 0, values[2], values[5], 1};
  onSetData(name, data, sizeof(data));
}

void UniformBuffer::onSetData(const std::string& name, const void* data, size_t size) {
  auto key = name + nameSuffix;
  auto result = uniformMap.find(key);
  if (result == uniformMap.end()) {
    LOGE("UniformBuffer::onSetData() uniform '%s' not found!", name.c_str());
    return;
  }
  auto index = result->second;
  DEBUG_ASSERT(uniforms[index].size() == size);
  memcpy(_buffer + offsets[index], data, size);
}

}  // namespace tgfx
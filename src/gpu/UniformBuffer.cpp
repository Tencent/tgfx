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
UniformBuffer::UniformBuffer(std::vector<Uniform> uniforms, bool uboSupport)
    : _uniforms(std::move(uniforms)), _uboSupport(uboSupport) {
  if (_uniforms.empty()) {
    LOGE("UniformBuffer constructor called with no uniforms provided.");
    return;
  }

  uniformBufferLayout = std::make_unique<UniformBufferLayout>(_uboSupport);

  for (const auto& uniform : _uniforms) {
    uniformBufferLayout->add(uniform);
  }

  if (const size_t totalSize = uniformBufferLayout->totalSize(); totalSize > 0) {
    buffer.resize(totalSize);
  }
}

void UniformBuffer::setData(const std::string& name, const Matrix& matrix) {
  float values[6] = {};
  matrix.get6(values);

  if (_uboSupport) {
    // clang-format off
    const float data[] = {
      values[0], values[3], 0, 0,
      values[1], values[4], 0, 0,
      values[2], values[5], 1, 0,
    };
    // clang-format on
    onSetData(name, data, sizeof(data));
  } else {
    // clang-format off
    const float data[] = {
      values[0], values[3], 0,
      values[1], values[4], 0,
      values[2], values[5], 1
    };
    // clang-format on
    onSetData(name, data, sizeof(data));
  }
}

void UniformBuffer::onSetData(const std::string& name, const void* data, size_t size) {
  const auto& key = name + nameSuffix;
  const auto* field = uniformBufferLayout->findField(key);

  if (field == nullptr) {
    LOGE("UniformBuffer::onSetData() uniform '%s' not found!", name.c_str());
    return;
  }
  DEBUG_ASSERT(field->size == size);

#if 0
  LOGI("------ UniformBuffer::onSetData ------");
  LOGI("name: %-20s, format: %-10s, size: %zu", name.c_str(), ToUniformFormatName(field->format), size);
  for (size_t i = 0; i < size; i++) {
    printf("%02X ", ((const uint8_t*)data)[i]);
  }
  LOGI("");
#endif

  memcpy(buffer.data() + field->offset, data, size);
}

}  // namespace tgfx
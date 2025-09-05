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

#include <string>
#include <unordered_map>
#include <vector>
#include "gpu/Uniform.h"
#include "tgfx/core/Matrix.h"

namespace tgfx {
/**
 * An object representing the collection of uniform variables in a GPU program.
 */
class UniformBuffer {
 public:
  ~UniformBuffer();

  /**
   * Copies value into the uniform buffer. The data must have the same size as the uniform specified
   * by name.
   */
  template <typename T>
  std::enable_if_t<std::is_trivially_copyable_v<T> && !std::is_pointer_v<T>, void> setData(
      const std::string& name, const T& value) {
    onSetData(name, &value, sizeof(value));
  }

  /**
   * Convenience method for copying a Matrix to a 3x3 matrix in column-major order.
   */
  void setData(const std::string& name, const Matrix& matrix);

  /**
   * Returns a pointer to the start of the uniform buffer in memory.
   */
  const void* data() const {
    return _buffer;
  }

  /**
   * Returns the size of the uniform buffer in bytes.
   */
  size_t size() const {
    return offsets.back();
  }

  /**
   * Returns the list of uniforms in this buffer.
   */
  const std::vector<Uniform>& uniforms() const {
    return _uniforms;
  }

 private:
  uint8_t* _buffer = nullptr;
  std::vector<Uniform> _uniforms = {};
  std::unordered_map<std::string, size_t> uniformMap = {};
  std::vector<size_t> offsets = {};
  std::string nameSuffix = "";

  UniformBuffer(std::vector<Uniform> uniforms, std::unordered_map<std::string, size_t> uniformMap);

  void onSetData(const std::string& name, const void* data, size_t size);

  friend class ProgramInfo;
  friend class UniformHandler;
};
}  // namespace tgfx

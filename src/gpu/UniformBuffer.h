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
#include "gpu/ShaderVar.h"
#include "tgfx/core/Matrix.h"

namespace tgfx {
/**
 * Represents a uniform variable in a GPU program.
 */
class Uniform {
 public:
  /**
   * Default constructor for Uniform.
   */
  Uniform() = default;

  /**
   * Creates a uniform variable with the specified name, type, and visibility.
   */
  Uniform(const std::string& name, SLType type, ShaderFlags visibility)
      : _name(name), _type(type), _visibility(visibility) {
  }

  /**
   * The name of the uniform variable.
   */
  std::string name() const {
    return _name;
  }

  /**
   * The type of the uniform variable.
   */
  SLType type() const {
    return _type;
  }

  /**
   * The visibility of the uniform variable, indicating which shaders can access it.
   */
  ShaderFlags visibility() const {
    return _visibility;
  }

  /**
   * Returns the size of the uniform variable in bytes.
   */
  size_t size() const {
    return GetSLTypeSize(_type);
  }

 private:
  std::string _name = {};
  SLType _type = SLType::Void;
  ShaderFlags _visibility = ShaderFlags::None;
};

/**
 * An object representing the collection of uniform variables in a GPU program.
 */
class UniformBuffer {
 public:
  /**
   * Constructs a uniform buffer with the specified uniforms.
   */
  explicit UniformBuffer(std::vector<Uniform> uniforms);

  virtual ~UniformBuffer() = default;

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

 protected:
  std::vector<Uniform> uniforms = {};
  std::vector<size_t> offsets = {};

  /**
   * Copies data into the uniform buffer. The data must have the same size as the uniform specified
   * by name.
   */
  virtual void onCopyData(size_t index, size_t offset, size_t size, const void* data) = 0;

 private:
  std::string nameSuffix = "";
  std::unordered_map<std::string, size_t> uniformMap = {};

  void onSetData(const std::string& name, const void* data, size_t size);

  friend class Pipeline;
};
}  // namespace tgfx

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
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include <string>

namespace tgfx {
/**
 * Uniform variable formats.
 *
 * One value per shader type the declaration layer can express (see ShaderType in
 * gpu/shaders/Module.h): the derived layout stores it per field and the pipeline descriptor
 * compares it against the geometry's vertex attribute formats.
 */
enum class UniformFormat {
  Float = 0,                    // 32-bit floating point scalar.
  Float2 = 1,                   // 2-component vector of 32-bit floating point values.
  Float3 = 2,                   // 3-component vector of 32-bit floating point values.
  Float4 = 3,                   // 4-component vector of 32-bit floating point values.
  Float2x2 = 4,                 // 2x2 matrix of 32-bit floating point values.
  Float3x3 = 5,                 // 3x3 matrix of 32-bit floating point values.
  Float4x4 = 6,                 // 4x4 matrix of 32-bit floating point values.
  Int = 7,                      // 32-bit signed integer scalar.
  Int2 = 8,                     // 2-component vector of 32-bit signed integer values.
  Int3 = 9,                     // 3-component vector of 32-bit signed integer values.
  Int4 = 10,                    // 4-component vector of 32-bit signed integer values.
  Texture2DSampler = 11,        // 2D texture sampler.
  TextureExternalSampler = 12,  // External texture sampler (e.g. for camera input).
  Texture2DRectSampler = 13,    // Rectangle texture sampler.
};

static_assert(static_cast<int>(UniformFormat::Float) == 0 &&
                  static_cast<int>(UniformFormat::Float4x4) == 6 &&
                  static_cast<int>(UniformFormat::Int) == 7 &&
                  static_cast<int>(UniformFormat::Int4) == 10 &&
                  static_cast<int>(UniformFormat::Texture2DSampler) == 11 &&
                  static_cast<int>(UniformFormat::Texture2DRectSampler) == 13,
              "the format values are pinned; append new formats instead of renumbering");

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
   * Creates a uniform variable with the specified name, type, and element count. An array uniform
   * is created when count is greater than 1. Array uniforms must use a format whose element size is
   * 16 bytes (e.g. Float4), because the std140 layout aligns every array element to 16 bytes.
   */
  Uniform(std::string name, UniformFormat format, uint32_t count = 1)
      : _name(std::move(name)), _format(format), _count(count) {
  }

  /**
   * Returns true if the uniform variable is empty.
   */
  bool empty() const {
    return _name.empty();
  }

  /**
   * The name of the uniform variable.
   */
  const std::string& name() const {
    return _name;
  }

  /**
   * The format of the uniform variable.
   */
  UniformFormat format() const {
    return _format;
  }

  /**
   * The number of elements in the uniform array. Returns 1 for scalar uniforms.
   */
  uint32_t count() const {
    return _count;
  }

  /**
   * Returns the total size of the uniform variable in bytes.
   */
  size_t size() const;

 private:
  std::string _name = {};
  UniformFormat _format = UniformFormat::Float;
  uint32_t _count = 1;
};
}  // namespace tgfx

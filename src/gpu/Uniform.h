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

#include <cstdint>
#include <string>

namespace tgfx {
/**
 * Uniform variable formats.
 *
 * The numeric values are part of the precompiled bundle format: shader_build_tool stores a
 * UniformFormat as one byte in every reflection entry and the runtime loader reads it back. The
 * values are therefore written out and pinned below. Never reorder or renumber an entry, and
 * append new formats at the end; any change to an existing value or to the meaning of a stored
 * format must bump ExpectedToolchainABI in PrecompiledBundleIdentity.h.
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
              "UniformFormat values are stored in the bundle format; see the comment above.");

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
   * is created when arraySize is greater than 1. Array uniforms must use a format whose element
   * size is 16 bytes (e.g. Float4), because the std140 layout aligns every array element to 16
   * bytes.
   */
  Uniform(std::string name, UniformFormat format, uint32_t arraySize = 1)
      : _name(std::move(name)), _format(format), _arraySize(arraySize) {
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
  uint32_t arraySize() const {
    return _arraySize;
  }

  /**
   * Returns the total size of the uniform variable in bytes.
   */
  size_t size() const;

 private:
  std::string _name = {};
  UniformFormat _format = UniformFormat::Float;
  uint32_t _arraySize = 1;
};
}  // namespace tgfx

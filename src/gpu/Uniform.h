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

namespace tgfx {
/**
 * Uniform variable formats.
 */
enum class UniformFormat {
  /**
   * 32-bit floating point scalar.
   */
  Float,

  /**
   * 2-component vector of 32-bit floating point values.
   */
  Float2,

  /**
   * 3-component vector of 32-bit floating point values.
   */
  Float3,

  /**
   * 4-component vector of 32-bit floating point values.
   */
  Float4,

  /**
   * 2x2 matrix of 32-bit floating point values.
   */
  Float2x2,

  /**
   * 3x3 matrix of 32-bit floating point values.
   */
  Float3x3,

  /**
   * 4x4 matrix of 32-bit floating point values.
   */
  Float4x4,

  /**
   * 32-bit signed integer scalar.
   */
  Int,

  /**
   * 2-component vector of 32-bit signed integer values.
   */
  Int2,

  /**
   * 3-component vector of 32-bit signed integer values.
   */
  Int3,

  /**
   * 4-component vector of 32-bit signed integer values.
   */
  Int4,

  /**
   * 2D texture sampler.
   */
  Texture2DSampler,

  /**
   * External texture sampler (e.g. for camera input).
   */
  TextureExternalSampler,

  /**
   * Rectangle texture sampler.
   */
  Texture2DRectSampler,
};

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
  Uniform(std::string name, UniformFormat format) : _name(std::move(name)), _format(format) {
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
   * Returns the size of the uniform variable in bytes.
   */
  size_t size() const;

 private:
  std::string _name = {};
  UniformFormat _format = UniformFormat::Float;
};
}  // namespace tgfx

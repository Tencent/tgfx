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
 * Vertex attribute formats.
 */
enum class VertexFormat {
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
   * 16-bit floating point scalar.
   */
  Half,

  /**
   * 2-component vector of 16-bit floating point values.
   */
  Half2,

  /**
   * 3-component vector of 16-bit floating point values.
   */
  Half3,

  /**
   * 4-component vector of 16-bit floating point values.
   */
  Half4,

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
   * 8-bit unsigned integer scalar, normalized to [0,1].
   */
  UByteNormalized,

  /**
   * 2-component vector of 8-bit unsigned integer values, normalized to [0,1].
   */
  UByte2Normalized,

  /**
   * 3-component vector of 8-bit unsigned integer values, normalized to [0,1].
   */
  UByte3Normalized,

  /**
   * 4-component vector of 8-bit unsigned integer values, normalized to [0,1].
   */
  UByte4Normalized,
};

/**
  * Represents a vertex attribute. in a GPU program.
  */
class Attribute {
 public:
  Attribute() = default;

  /**
   * Creates an attribute with the specified name and format.
   */
  Attribute(std::string name, VertexFormat format) : _name(std::move(name)), _format(format) {
  }

  /**
   * Returns true if the attribute is empty.
   */
  bool empty() const {
    return _name.empty();
  }

  /**
   * Returns the name of the attribute.
   */
  const std::string& name() const {
    return _name;
  }

  /**
   * Returns the format of the attribute.
   */
  VertexFormat format() const {
    return _format;
  }

  /**
   * Returns the size of the attribute in bytes.
   */
  size_t size() const;

 private:
  std::string _name = {};
  VertexFormat _format = VertexFormat::Float;
};
}  // namespace tgfx

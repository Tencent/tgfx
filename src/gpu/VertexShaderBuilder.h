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

#include <cstdint>
#include <string>
#include <vector>
#include "ShaderBuilder.h"
#include "gpu/ir/ProgramDescription.h"

namespace tgfx {
static const std::string RTAdjustName = "tgfx_RTAdjust";

class VertexShaderBuilder : public ShaderBuilder {
 public:
  explicit VertexShaderBuilder(ProgramBuilder* program) : ShaderBuilder(program) {
  }

  ShaderStage shaderStage() const override {
    return ShaderStage::Vertex;
  }

  virtual void emitNormalizedPosition(const std::string& devPos) = 0;

  /**
   * Emits GLSL code to transform a 2D point by a 3x3 matrix.
   * @param dstPointName The name of the destination vec2 variable to store the result.
   * @param srcPointName The name of the source point (vec2).
   * @param transformName The name of the mat3 transform.
   * @param hasPerspective If true, performs perspective division (divide by w).
   */
  virtual void emitTransformedPoint(const std::string& dstPointName,
                                    const std::string& srcPointName,
                                    const std::string& transformName, bool hasPerspective) = 0;

  /**
   * Stores one attribute or local into a varying.
   */
  void storeVarying(const std::string& dstName, const std::string& srcName);

  /**
   * Transforms a 2D point by the named mat3 and stores the result in a local.
   */
  void transformPoint(const std::string& dstPointName, const std::string& srcPointName,
                      const std::string& transformName, bool hasPerspective);

  /**
   * Transforms a 2D point by the named mat3 and stores the result in a varying.
   */
  void transformVarying(const std::string& varyingName, const std::string& srcPointName,
                        const std::string& transformName, bool hasPerspective);

  /**
   * Transforms a 2D point by the named mat3, adds the named offset and stores the result in a local.
   */
  void transformPointAdd(const std::string& dstPointName, const std::string& srcPointName,
                         const std::string& transformName, const std::string& offsetName);

  /**
   * Multiplies two vec2 values and stores the result in a local.
   */
  void multiplyVec2(const std::string& dstName, const std::string& first, const std::string& second,
                    bool declare = false);

  /**
   * Stores one half of a vec4 value in a vec2 local; components is "xy" or "zw".
   */
  void swizzleVec2(const std::string& dstName, const std::string& srcName,
                   const std::string& components);

  /**
   * Composes two vec2 values into a vec4 local.
   */
  void composeVec4(const std::string& dstName, const std::string& first, const std::string& second);

  /**
   * Normalizes a rect's two corners in place: the low half becomes the min, the high half the max.
   */
  void normalizeRect(const std::string& rectName);

  /**
   * Writes the clip-space position the stage's normalized device coordinates come from.
   */
  void storePosition(const std::string& devPos);
};
}  // namespace tgfx

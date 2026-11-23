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

namespace tgfx {
static const std::string RTAdjustName = "tgfx_RTAdjust";

/// Which write one vertex-stage record carries: the whole vertex emit surface the geometry
/// processors use, namely attribute reads stored into varyings, the coordinate transforms the FP
/// tree's varyings come from, and the clip-space write. The bytecode routes turn each record into
/// SPIR-V; the GLSL builder prints the same text it always printed.
enum class VertexCallKind : uint8_t {
  /// dst = src, an attribute or a prior local stored into a varying.
  StoreVarying,
  /// dst = (matrix * vec3(src, 1)).xy, or the perspective-divided form when hasPerspective.
  TransformPoint,
  /// dst = matrix * vec3(src, 1) written to a varying: the vec3 form carries the w for the
  /// fragment stage's divide, the vec2 form drops it at the vertex stage.
  TransformVarying,
  /// dst = (matrix * vec3(src, 1)).xy + extra.
  TransformPointAdd,
  /// dst = src * operand, two vec2 values.
  MultiplyVec2,
  /// dst = src.xy or src.zw: the two halves of a rect attribute.
  SwizzleVec2,
  /// dst = vec4(src, operand), two vec2 values side by side.
  ComposeVec4,
  /// src = vec4(min(src.xy, src.zw), max(src.xy, src.zw)), in place.
  NormalizeRect,
  /// gl_Position = vec4(src.xy * RTAdjust.xz + RTAdjust.yw, 0, 1).
  StorePosition,
};

/// One vertex-stage write in emission order. A backend that emits a stage from these records must
/// check that every declared varying has a producing record: a geometry still spelling part of its
/// vertex stage as text leaves a varying unproduced, and that check turns the case into a declined
/// draw instead of a silently wrong shader.
struct VertexCallRecord {
  VertexCallKind kind = VertexCallKind::StoreVarying;
  /// The varying or local the record writes; unused by StorePosition.
  std::string dst = {};
  /// The value operand: an attribute's or an earlier local's name, the clip-space source.
  std::string src = {};
  /// The transform's matrix uniform, or MultiplyVec2's second factor.
  std::string operand = {};
  /// TransformPointAdd's addend.
  std::string extra = {};
  bool hasPerspective = false;
};

class VertexShaderBuilder : public ShaderBuilder {
 public:
  explicit VertexShaderBuilder(ProgramBuilder* program) : ShaderBuilder(program) {
  }

  ShaderStage shaderStage() const override {
    return ShaderStage::Vertex;
  }

  /**
   * The sink a bytecode builder installs to collect the structured writes; nullptr keeps the
   * records off (the GLSL route). The records append in emission order.
   */
  void setVertexRecorder(std::vector<VertexCallRecord>* records) {
    vertexRecorder = records;
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

 protected:
  /// Appends one record when a recorder is installed; the callers print the text either way.
  void recordVertexCall(VertexCallKind kind, const std::string& dst, const std::string& src,
                        const std::string& operand, const std::string& extra, bool hasPerspective);

 private:
  std::vector<VertexCallRecord>* vertexRecorder = nullptr;
};
}  // namespace tgfx

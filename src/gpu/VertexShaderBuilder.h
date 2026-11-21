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

#include "ShaderBuilder.h"

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
};
}  // namespace tgfx

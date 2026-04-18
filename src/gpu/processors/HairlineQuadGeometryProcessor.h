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

#include <optional>
#include <string>
#include "GeometryProcessor.h"
#include "gpu/AAType.h"
#include "tgfx/core/BytesKey.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/gpu/Attribute.h"

namespace tgfx {

/**
 * HairlineQuadGeometryProcessor is used to render hairline quadratic curve segments.
 */
class HairlineQuadGeometryProcessor : public GeometryProcessor {
 public:
  static PlacementPtr<HairlineQuadGeometryProcessor> Make(BlockAllocator* allocator,
                                                          const PMColor& color,
                                                          const Matrix& viewMatrix,
                                                          std::optional<Matrix> uvMatrix,
                                                          float coverage, AAType aaType);

  std::string name() const override {
    return "HairlineQuadGeometryProcessor";
  }

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  HairlineQuadGeometryProcessor(const PMColor& color, const Matrix& viewMatrix,
                                std::optional<Matrix> uvMatrix, float coverage, AAType aaType);

  void onComputeProcessorKey(BytesKey* bytesKey) const override;

  void onBuildShaderMacros(ShaderMacroSet& macros) const override {
    if (aaType == AAType::Coverage) {
      macros.define("TGFX_GP_HQUAD_COVERAGE_AA");
    }
  }

  std::string shaderFunctionFile() const override {
    return "geometry/hairline_quad_geometry.vert";
  }

  ShaderCallManifest buildColorCallExpr(const MangledUniforms& uniforms,
                                        const MangledVaryings& /*varyings*/) const override {
    ShaderCallManifest result;
    result.outputVarName = "gpColor";
    result.statement = "vec4 gpColor = " + uniforms.get("Color") + ";\n";
    return result;
  }

  ShaderCallManifest buildCoverageCallExpr(const MangledUniforms& uniforms,
                                           const MangledVaryings& varyings) const override {
    ShaderCallManifest result;
    result.outputVarName = "gpCoverage";
    auto edge = varyings.get("HairQuadEdge");
    auto covScale = uniforms.get("Coverage");
    std::string code;
    code += "float edgeAlpha;\n";
    code += "vec2 duvdx = vec2(dFdx(" + edge + ".xy));\n";
    code += "vec2 duvdy = vec2(dFdy(" + edge + ".xy));\n";
    code += "vec2 gF = vec2(2.0 * " + edge + ".x * duvdx.x - duvdx.y,";
    code += "               2.0 * " + edge + ".x * duvdy.x - duvdy.y);\n";
    code += "edgeAlpha = float(" + edge + ".x * " + edge + ".x - " + edge + ".y);\n";
    code += "edgeAlpha = sqrt(edgeAlpha * edgeAlpha / dot(gF, gF));\n";
    code += "edgeAlpha = max(1.0 - edgeAlpha, 0.0);\n";
    if (aaType != AAType::Coverage) {
      code += "edgeAlpha = edgeAlpha >= 0.5 ? 1.0 : 0.0;\n";
    }
    code += "vec4 gpCoverage = vec4(" + covScale + " * edgeAlpha);\n";
    result.statement = code;
    return result;
  }

  PMColor color = {};
  Matrix viewMatrix = {};
  std::optional<Matrix> uvMatrix = std::nullopt;
  float coverage = 1.0f;
  AAType aaType = AAType::None;

  Attribute position = {};
  Attribute hairQuadEdge = {};
};

}  // namespace tgfx

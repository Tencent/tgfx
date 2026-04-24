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

#include <optional>
#include "GeometryProcessor.h"
#include "gpu/AAType.h"

namespace tgfx {
class RoundStrokeRectGeometryProcessor : public GeometryProcessor {
 public:
  static PlacementPtr<RoundStrokeRectGeometryProcessor> Make(BlockAllocator* allocator,
                                                             AAType aaType,
                                                             std::optional<PMColor> commonColor,
                                                             std::optional<Matrix> uvMatrix);

  std::string name() const override {
    return "RoundStrokeRectGeometryProcessor";
  }

 protected:
  DEFINE_PROCESSOR_CLASS_ID
  RoundStrokeRectGeometryProcessor(AAType aa, std::optional<PMColor> commonColor,
                                   std::optional<Matrix> uvMatrix);
  void onComputeProcessorKey(BytesKey* bytesKey) const override;

  void onBuildShaderMacros(ShaderMacroSet& macros) const override {
    if (aaType == AAType::Coverage) {
      macros.define("TGFX_GP_RRECT_COVERAGE_AA");
    }
    if (commonColor.has_value()) {
      macros.define("TGFX_GP_RRECT_COMMON_COLOR");
    }
    if (uvMatrix.has_value()) {
      macros.define("TGFX_GP_RRECT_UV_MATRIX");
    }
  }

  std::string shaderFunctionFile() const override {
    return "geometry/round_stroke_rect_geometry.vert";
  }

  std::string buildVSCallExpr(const MangledUniforms& /*uniforms*/,
                              const MangledVaryings& varyings) const override {
    std::string code = "highp vec2 position;\n";
    code += "TGFX_RoundStrokeRectGP_VS(" + std::string(inPosition.name()) + ", " +
            std::string(inEllipseOffset.name());
    if (aaType == AAType::Coverage) {
      code += ", " + std::string(inCoverage.name()) + ", " + std::string(inEllipseRadii.name()) +
              ", " + varyings.get("Coverage") + ", " + varyings.get("EllipseRadii");
    }
    if (!commonColor.has_value()) {
      code += ", " + std::string(inColor.name()) + ", " + varyings.get("Color");
    }
    code += ", " + varyings.get("EllipseOffsets") + ", position);\n";
    code += "gl_Position = TGFX_NormalizePosition(position);\n";
    return code;
  }

  std::string coordTransformInputExpr(const MangledUniforms& /*uniforms*/,
                                      const MangledVaryings& /*varyings*/) const override {
    return inUVCoord.empty() ? inPosition.name() : inUVCoord.name();
  }

  ShaderCallManifest buildColorCallExpr(const MangledUniforms& uniforms,
                                        const MangledVaryings& varyings) const override {
    ShaderCallManifest result;
    result.outputVarName = "gpColor";
    if (commonColor.has_value()) {
      result.statement = "vec4 gpColor = " + uniforms.get("Color") + ";\n";
    } else {
      result.statement = "vec4 gpColor = " + varyings.get("Color") + ";\n";
    }
    return result;
  }

  ShaderCallManifest buildCoverageCallExpr(const MangledUniforms& /*uniforms*/,
                                           const MangledVaryings& varyings) const override {
    ShaderCallManifest result;
    result.outputVarName = "gpCoverage";
    auto offsets = varyings.get("EllipseOffsets");
    std::string code;
    if (aaType == AAType::Coverage) {
      auto cov = varyings.get("Coverage");
      auto radii = varyings.get("EllipseRadii");
      code += "vec4 gpCoverage = vec4(" + cov + ");\n";
      code += "vec2 offset = " + offsets + ";\n";
      code += "if (dot(offset, offset) > 0.5) {\n";
      code += "  gpCoverage *= TGFX_EllipseOuterCoverage(offset, " + radii + ");\n";
      code += "}\n";
    } else {
      code += "vec2 offset = " + offsets + ";\n";
      code += "float edgeAlpha = step(dot(offset, offset), 1.0);\n";
      code += "vec4 gpCoverage = vec4(edgeAlpha);\n";
    }
    result.statement = code;
    return result;
  }

  bool hasUVPerspective() const override {
    return uvMatrix.has_value() && (uvMatrix->getType() & Matrix::PerspectiveMask) != 0;
  }

  Attribute inPosition;
  Attribute inCoverage;
  Attribute inEllipseOffset;
  Attribute inEllipseRadii;
  Attribute inUVCoord;
  Attribute inColor;

  AAType aaType = AAType::None;
  std::optional<PMColor> commonColor = std::nullopt;
  std::optional<Matrix> uvMatrix = std::nullopt;
};
}  // namespace tgfx

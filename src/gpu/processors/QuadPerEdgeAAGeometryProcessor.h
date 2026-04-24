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
#include "tgfx/core/Paint.h"

namespace tgfx {
class QuadPerEdgeAAGeometryProcessor : public GeometryProcessor {
 public:
  static PlacementPtr<QuadPerEdgeAAGeometryProcessor> Make(BlockAllocator* allocator, int width,
                                                           int height, AAType aa,
                                                           std::optional<PMColor> commonColor,
                                                           std::optional<Matrix> uvMatrix,
                                                           bool hasSubset);
  std::string name() const override {
    return "QuadPerEdgeAAGeometryProcessor";
  }

 protected:
  DEFINE_PROCESSOR_CLASS_ID
  QuadPerEdgeAAGeometryProcessor(int width, int height, AAType aa,
                                 std::optional<PMColor> commonColor, std::optional<Matrix> uvMatrix,
                                 bool hasSubset);

  void onComputeProcessorKey(BytesKey* bytesKey) const override;

  void onBuildShaderMacros(ShaderMacroSet& macros) const override {
    if (aa == AAType::Coverage) {
      macros.define("TGFX_GP_QUAD_COVERAGE_AA");
    }
    if (commonColor.has_value()) {
      macros.define("TGFX_GP_QUAD_COMMON_COLOR");
    }
    if (uvMatrix.has_value()) {
      macros.define("TGFX_GP_QUAD_UV_MATRIX");
    }
    if (hasSubset) {
      macros.define("TGFX_GP_QUAD_SUBSET");
      if (uvMatrix.has_value()) {
        macros.define("TGFX_GP_QUAD_SUBSET_MATRIX");
      }
    }
  }

  std::string shaderFunctionFile() const override {
    return "geometry/quad_aa_geometry.vert";
  }

  std::string buildVSCallExpr(const MangledUniforms& /*uniforms*/,
                              const MangledVaryings& varyings) const override {
    std::string code = "highp vec2 position;\n";
    code += "TGFX_QuadAAGP_VS(" + std::string(position.name());
    if (aa == AAType::Coverage) {
      code += ", " + std::string(coverage.name()) + ", " + varyings.get("Coverage");
    }
    if (!commonColor.has_value()) {
      code += ", " + std::string(color.name()) + ", " + varyings.get("Color");
    }
    code += ", position);\n";
    code += "gl_Position = TGFX_NormalizePosition(position);\n";
    return code;
  }

  std::string coordTransformInputExpr(const MangledUniforms& /*uniforms*/,
                                      const MangledVaryings& /*varyings*/) const override {
    return uvCoord.empty() ? position.name() : uvCoord.name();
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
    if (aa == AAType::Coverage) {
      result.statement = "vec4 gpCoverage = vec4(" + varyings.get("Coverage") + ");\n";
    } else {
      result.statement = "vec4 gpCoverage = vec4(1.0);\n";
    }
    return result;
  }

  bool hasUVPerspective() const override {
    return uvMatrix.has_value() && (uvMatrix->getType() & Matrix::PerspectiveMask) != 0;
  }

  Attribute position;  // May contain coverage as last channel
  Attribute coverage;
  Attribute uvCoord;
  Attribute color;
  Attribute subset;

  int width = 1;
  int height = 1;
  AAType aa = AAType::None;
  std::optional<PMColor> commonColor = std::nullopt;
  std::optional<Matrix> uvMatrix = std::nullopt;
  bool hasSubset = false;
};
}  // namespace tgfx

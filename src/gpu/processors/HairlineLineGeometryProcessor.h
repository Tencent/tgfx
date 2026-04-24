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
#include <vector>
#include "GeometryProcessor.h"
#include "gpu/AAType.h"
#include "gpu/variants/ShaderVariant.h"
#include "tgfx/core/BytesKey.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/gpu/Attribute.h"

namespace tgfx {

/**
 * HairlineLineGeometryProcessor is used to render hairline line segments.
 */
class HairlineLineGeometryProcessor : public GeometryProcessor {
 public:
  static PlacementPtr<HairlineLineGeometryProcessor> Make(BlockAllocator* allocator,
                                                          const PMColor& color,
                                                          const Matrix& viewMatrix,
                                                          std::optional<Matrix> uvMatrix,
                                                          float coverage, AAType aaType);

  std::string name() const override {
    return "HairlineLineGeometryProcessor";
  }

  /**
   * Populates the given ShaderMacroSet with the preprocessor defines this processor emits for
   * the specified AA mode.
   */
  static void BuildMacros(AAType aaType, ShaderMacroSet& macros);

  /**
   * Returns the full set of shader variants: (aa in {None/MSAA, Coverage}) = 2 variants.
   */
  static std::vector<ShaderVariant> EnumerateVariants();

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  HairlineLineGeometryProcessor(const PMColor& color, const Matrix& viewMatrix,
                                std::optional<Matrix> uvMatrix, float coverage, AAType aaType);

  void onComputeProcessorKey(BytesKey* bytesKey) const override;

  void onBuildShaderMacros(ShaderMacroSet& macros) const override {
    BuildMacros(aaType, macros);
  }

  std::string shaderFunctionFile() const override {
    return "geometry/hairline_line_geometry.vert";
  }

  std::string buildVSCallExpr(const MangledUniforms& uniforms,
                              const MangledVaryings& varyings) const override {
    std::string code = "TGFX_HairlineLineGP_VS(";
    code += std::string(position.name()) + ", ";
    code += std::string(edgeDistance.name()) + ", ";
    code += uniforms.get("Matrix") + ", ";
    code += varyings.get("EdgeDistance") + ", ";
    code += varyings.get("TransformedPosition") + ");\n";
    return code;
  }

  std::string coordTransformInputExpr(const MangledUniforms& /*uniforms*/,
                                      const MangledVaryings& varyings) const override {
    return varyings.get("TransformedPosition");
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
    auto edge = varyings.get("EdgeDistance");
    auto covScale = uniforms.get("Coverage");
    std::string code;
    code += "float edgeAlpha = TGFX_HairlineLineCoverage(" + edge + ");\n";
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
  Attribute edgeDistance = {};
};

}  // namespace tgfx

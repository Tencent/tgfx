/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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
#include <vector>
#include "GeometryProcessor.h"
#include "gpu/variants/ShaderVariant.h"

namespace tgfx {
/**
 * NonAARRectGeometryProcessor is used to render round rectangles without antialiasing.
 * It evaluates the round rect shape in the fragment shader using local coordinates.
 * Supports both fill and stroke modes.
 */
class NonAARRectGeometryProcessor : public GeometryProcessor {
 public:
  static PlacementPtr<NonAARRectGeometryProcessor> Make(BlockAllocator* allocator, int width,
                                                        int height, bool stroke,
                                                        std::optional<PMColor> commonColor);

  std::string name() const override {
    return "NonAARRectGeometryProcessor";
  }

  /**
   * Populates the given ShaderMacroSet with the preprocessor defines this processor emits for
   * the specified (commonColor, stroke) configuration.
   */
  static void BuildMacros(bool commonColor, bool stroke, ShaderMacroSet& macros);

  /**
   * Returns the full set of shader variants:
   *   (commonColor) x (stroke) = 4 variants.
   * Stable index: commonColor * 2 + stroke.
   */
  static std::vector<ShaderVariant> EnumerateVariants();

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  NonAARRectGeometryProcessor(int width, int height, bool stroke,
                              std::optional<PMColor> commonColor);

  void onComputeProcessorKey(BytesKey* bytesKey) const override;

  void onBuildShaderMacros(ShaderMacroSet& macros) const override {
    BuildMacros(commonColor.has_value(), stroke, macros);
  }

  std::string shaderFunctionFile() const override {
    return "geometry/non_aa_rrect_geometry.vert";
  }

  std::string buildVSCallExpr(const MangledUniforms& /*uniforms*/,
                              const MangledVaryings& varyings) const override {
    std::string code = "highp vec2 position;\n";
    code += "TGFX_NonAARRectGP_VS(" + std::string(inPosition.name()) + ", " +
            std::string(inLocalCoord.name()) + ", " + std::string(inRadii.name()) + ", " +
            std::string(inRectBounds.name());
    if (!commonColor.has_value()) {
      code += ", " + std::string(inColor.name()) + ", " + varyings.get("Color");
    }
    if (stroke) {
      code += ", " + std::string(inStrokeWidth.name()) + ", " + varyings.get("strokeWidth");
    }
    code += ", " + varyings.get("localCoord") + ", " + varyings.get("radii") + ", " +
            varyings.get("rectBounds") + ", position);\n";
    code += "gl_Position = TGFX_NormalizePosition(position);\n";
    return code;
  }

  std::string coordTransformInputExpr(const MangledUniforms& /*uniforms*/,
                                      const MangledVaryings& /*varyings*/) const override {
    return inPosition.name();
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
    auto lc = varyings.get("localCoord");
    auto rd = varyings.get("radii");
    auto bn = varyings.get("rectBounds");
    std::string code;
    code += "float coverage = TGFX_RRectOuterCoverage(" + lc + ", " + bn + ", " + rd + ");\n";
    if (stroke) {
      auto sw = varyings.get("strokeWidth");
      code += "float inner = TGFX_RRectInnerCoverage(" + lc + ", " + bn + ", " + rd + ", " + sw +
              ");\n";
      code += "coverage *= (1.0 - inner);\n";
    }
    code += "vec4 gpCoverage = vec4(coverage);\n";
    result.statement = code;
    return result;
  }

  // Vertex attributes - declared in the same order as vertex data layout.
  Attribute inPosition;     // position (2 floats)
  Attribute inLocalCoord;   // local coordinates (2 floats)
  Attribute inRadii;        // corner radii (2 floats)
  Attribute inRectBounds;   // rect bounds: left, top, right, bottom (4 floats)
  Attribute inColor;        // optional color (4 bytes as UByte4Normalized)
  Attribute inStrokeWidth;  // half stroke width (2 floats, stroke only)

  int width = 1;
  int height = 1;
  bool stroke = false;
  std::optional<PMColor> commonColor = std::nullopt;
};
}  // namespace tgfx

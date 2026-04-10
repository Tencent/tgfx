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

namespace tgfx {
/**
 * Skia's sharing：
 * https://www.essentialmath.com/GDC2015/VanVerth_Jim_DrawingAntialiasedEllipse.pdf
 *
 * There is a formula that calculates the approximate distance from the point to the ellipse,
 * and the proof of the formula can be found in the link below.
 * https://www.researchgate.net/publication/222440289_Sampson_PD_Fitting_conic_sections_to_very_scattered_data_An_iterative_refinement_of_the_Bookstein_algorithm_Comput_Graphics_Image_Process_18_97-108
 */
class EllipseGeometryProcessor : public GeometryProcessor {
 public:
  static PlacementPtr<EllipseGeometryProcessor> Make(BlockAllocator* allocator, int width,
                                                     int height, bool stroke,
                                                     std::optional<PMColor> commonColor);

  std::string name() const override {
    return "EllipseGeometryProcessor";
  }

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  EllipseGeometryProcessor(int width, int height, bool stroke, std::optional<PMColor> commonColor);

  void onComputeProcessorKey(BytesKey* bytesKey) const override;

  void onBuildShaderMacros(ShaderMacroSet& macros) const override {
    if (stroke) {
      macros.define("TGFX_GP_ELLIPSE_STROKE");
    }
    if (commonColor.has_value()) {
      macros.define("TGFX_GP_ELLIPSE_COMMON_COLOR");
    }
  }

  std::string shaderFunctionFile() const override {
    return "geometry/ellipse_geometry";
  }

  ShaderCallResult buildColorCallExpr(const MangledUniforms& uniforms,
                                      const MangledVaryings& varyings) const override {
    ShaderCallResult result;
    result.outputVarName = "gpColor";
    if (commonColor.has_value()) {
      result.statement = "vec4 gpColor = " + uniforms.get("Color") + ";\n";
    } else {
      result.statement = "vec4 gpColor = " + varyings.get("Color") + ";\n";
    }
    return result;
  }

  ShaderCallResult buildCoverageCallExpr(const MangledUniforms& /*uniforms*/,
                                         const MangledVaryings& varyings) const override {
    ShaderCallResult result;
    result.outputVarName = "gpCoverage";
    auto offsets = varyings.get("EllipseOffsets");
    auto radii = varyings.get("EllipseRadii");
    std::string code;
    code += "vec2 offset = " + offsets + ".xy;\n";
    if (stroke) {
      code += "offset *= " + radii + ".xy;\n";
    }
    code += "float test = dot(offset, offset) - 1.0;\n";
    code += "vec2 grad = 2.0*offset*" + radii + ".xy;\n";
    code += "float grad_dot = dot(grad, grad);\n";
    code += "grad_dot = max(grad_dot, 1.1755e-38);\n";
    code += "float invlen = inversesqrt(grad_dot);\n";
    code += "float edgeAlpha = clamp(0.5-test*invlen, 0.0, 1.0);\n";
    if (stroke) {
      code += "offset = " + offsets + ".xy*" + radii + ".zw;\n";
      code += "test = dot(offset, offset) - 1.0;\n";
      code += "grad = 2.0*offset*" + radii + ".zw;\n";
      code += "grad_dot = dot(grad, grad);\n";
      code += "invlen = inversesqrt(grad_dot);\n";
      code += "edgeAlpha *= clamp(0.5+test*invlen, 0.0, 1.0);\n";
    }
    code += "vec4 gpCoverage = vec4(edgeAlpha);\n";
    result.statement = code;
    return result;
  }

  Attribute inPosition;
  Attribute inColor;
  Attribute inEllipseOffset;
  Attribute inEllipseRadii;

  int width = 1;
  int height = 1;
  bool stroke;
  std::optional<PMColor> commonColor = std::nullopt;
};
}  // namespace tgfx

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

#include "GeometryProcessor.h"
#include "gpu/AAType.h"

namespace tgfx {
class DefaultGeometryProcessor : public GeometryProcessor {
 public:
  static PlacementPtr<DefaultGeometryProcessor> Make(BlockAllocator* allocator, PMColor color,
                                                     int width, int height, AAType aa,
                                                     const Matrix& viewMatrix,
                                                     const Matrix& uvMatrix);

  std::string name() const override {
    return "DefaultGeometryProcessor";
  }

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  DefaultGeometryProcessor(PMColor color, int width, int height, AAType aa,
                           const Matrix& viewMatrix, const Matrix& uvMatrix);

  void onComputeProcessorKey(BytesKey* bytesKey) const override;

  void onBuildShaderMacros(ShaderMacroSet& macros) const override {
    if (aa == AAType::Coverage) {
      macros.define("TGFX_GP_DEFAULT_COVERAGE_AA");
    }
  }

  std::string shaderFunctionFile() const override {
    return "geometry/default_geometry.vert";
  }

  std::string buildVSCallExpr(const MangledUniforms& uniforms,
                              const MangledVaryings& varyings) const override {
    std::string code = "highp vec2 position;\n";
    code += "TGFX_DefaultGP_VS(" + std::string(position.name()) + ", " + uniforms.get("Matrix");
    if (aa == AAType::Coverage) {
      code += ", " + std::string(coverage.name()) + ", " + varyings.get("Coverage");
    }
    code += ", position);\n";
    code += "gl_Position = vec4(position.xy * tgfx_RTAdjust.xz + tgfx_RTAdjust.yw, 0, 1);\n";
    return code;
  }

  ShaderCallResult buildColorCallExpr(const MangledUniforms& uniforms,
                                      const MangledVaryings& /*varyings*/) const override {
    ShaderCallResult result;
    result.outputVarName = "gpColor";
    result.statement = "vec4 gpColor = " + uniforms.get("Color") + ";\n";
    return result;
  }

  ShaderCallResult buildCoverageCallExpr(const MangledUniforms& /*uniforms*/,
                                         const MangledVaryings& varyings) const override {
    ShaderCallResult result;
    result.outputVarName = "gpCoverage";
    if (aa == AAType::Coverage) {
      result.statement = "vec4 gpCoverage = vec4(" + varyings.get("Coverage") + ");\n";
    } else {
      result.statement = "vec4 gpCoverage = vec4(1.0);\n";
    }
    return result;
  }

  bool hasUVPerspective() const override {
    return uvMatrix.hasPerspective();
  }

  Attribute position;
  Attribute coverage;

  PMColor color;
  int width = 1;
  int height = 1;
  AAType aa = AAType::None;
  Matrix viewMatrix = {};
  Matrix uvMatrix = {};
};
}  // namespace tgfx

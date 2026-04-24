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

#include <vector>
#include "GeometryProcessor.h"
#include "gpu/AAType.h"
#include "gpu/variants/ShaderVariant.h"

namespace tgfx {
class ShapeInstancedGeometryProcessor : public GeometryProcessor {
 public:
  static PlacementPtr<ShapeInstancedGeometryProcessor> Make(BlockAllocator* allocator, int width,
                                                            int height, AAType aa, bool hasColors,
                                                            const Matrix& uvMatrix,
                                                            const Matrix& viewMatrix);

  std::string name() const override {
    return "ShapeInstancedGeometryProcessor";
  }

  /**
   * Populates the given ShaderMacroSet with the preprocessor defines this processor emits for
   * the specified configuration.
   */
  static void BuildMacros(bool coverageAA, bool hasColors, ShaderMacroSet& macros);

  /**
   * Returns the full set of shader variants:
   *   (coverageAA) x (hasColors) = 4 variants.
   */
  static std::vector<ShaderVariant> EnumerateVariants();

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  ShapeInstancedGeometryProcessor(int width, int height, AAType aa, bool hasColors,
                                  const Matrix& uvMatrix, const Matrix& viewMatrix);

  void onComputeProcessorKey(BytesKey* bytesKey) const override;

  void onBuildShaderMacros(ShaderMacroSet& macros) const override {
    BuildMacros(aa == AAType::Coverage, hasColors, macros);
  }

  std::string shaderFunctionFile() const override {
    return "geometry/shape_instanced_geometry.vert";
  }

  std::string buildVSCallExpr(const MangledUniforms& uniforms,
                              const MangledVaryings& varyings) const override {
    std::string code = "TGFX_ShapeInstancedGP_VS(";
    code += std::string(position.name()) + ", ";
    code += std::string(offset.name()) + ", ";
    code += uniforms.get("ViewMatrix") + ", ";
    code += uniforms.get("UVMatrix");
    if (aa == AAType::Coverage) {
      code += ", " + std::string(coverage.name()) + ", " + varyings.get("Coverage");
    }
    if (hasColors) {
      code += ", " + std::string(instanceColor.name()) + ", " + varyings.get("InstanceColor");
    }
    code += ", " + varyings.get("LocalCoord") + ");\n";
    return code;
  }

  std::string coordTransformInputExpr(const MangledUniforms& /*uniforms*/,
                                      const MangledVaryings& varyings) const override {
    return varyings.get("LocalCoord");
  }

  ShaderCallManifest buildColorCallExpr(const MangledUniforms& /*uniforms*/,
                                        const MangledVaryings& varyings) const override {
    ShaderCallManifest result;
    result.outputVarName = "gpColor";
    if (hasColors) {
      result.statement = "vec4 gpColor = " + varyings.get("InstanceColor") + ";\n";
    } else {
      result.statement = "vec4 gpColor = vec4(1.0);\n";
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

  Attribute position = {};
  Attribute coverage = {};
  Attribute offset = {};
  Attribute instanceColor = {};

  int width = 1;
  int height = 1;
  AAType aa = AAType::None;
  bool hasColors = false;
  Matrix uvMatrix = {};
  Matrix viewMatrix = {};
};
}  // namespace tgfx

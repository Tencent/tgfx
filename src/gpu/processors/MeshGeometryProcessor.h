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
#include "gpu/variants/ShaderVariant.h"
#include "tgfx/core/Color.h"

namespace tgfx {

/**
 * Geometry processor for rendering mesh data with optional texture coordinates and vertex colors.
 */
class MeshGeometryProcessor : public GeometryProcessor {
 public:
  static PlacementPtr<MeshGeometryProcessor> Make(BlockAllocator* allocator, bool hasTexCoords,
                                                  bool hasColors, bool hasCoverage, PMColor color,
                                                  const Matrix& viewMatrix);

  std::string name() const override {
    return "MeshGeometryProcessor";
  }

  /**
   * Populates the given ShaderMacroSet with the preprocessor defines this processor emits for
   * the specified (hasTexCoords, hasColors, hasCoverage) configuration.
   */
  static void BuildMacros(bool hasTexCoords, bool hasColors, bool hasCoverage,
                          ShaderMacroSet& macros);

  /**
   * Returns the full set of shader variants:
   *   (hasTexCoords) x (hasColors) x (hasCoverage) = 8 variants.
   * Stable index: (texCoords << 2) | (colors << 1) | coverage.
   */
  static std::vector<ShaderVariant> EnumerateVariants();

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  MeshGeometryProcessor(bool hasTexCoords, bool hasColors, bool hasCoverage, PMColor color,
                        const Matrix& viewMatrix);

  void onComputeProcessorKey(BytesKey* bytesKey) const override;

  void onBuildShaderMacros(ShaderMacroSet& macros) const override {
    BuildMacros(hasTexCoords, hasColors, hasCoverage, macros);
  }

  std::string shaderFunctionFile() const override {
    return "geometry/mesh_geometry.vert";
  }

  std::string buildVSCallExpr(const MangledUniforms& uniforms,
                              const MangledVaryings& varyings) const override {
    std::string code = "highp vec2 position;\n";
    code += "TGFX_MeshGP_VS(" + std::string(position.name()) + ", " + uniforms.get("Matrix");
    if (hasTexCoords) {
      code += ", " + std::string(texCoord.name()) + ", " + varyings.get("TexCoord");
    }
    if (hasColors) {
      code += ", " + std::string(color.name()) + ", " + varyings.get("Color");
    }
    if (hasCoverage) {
      code += ", " + std::string(coverage.name()) + ", " + varyings.get("Coverage");
    }
    code += ", position);\n";
    code += "gl_Position = TGFX_NormalizePosition(position);\n";
    return code;
  }

  std::string coordTransformInputExpr(const MangledUniforms& /*uniforms*/,
                                      const MangledVaryings& /*varyings*/) const override {
    return hasTexCoords ? texCoord.name() : position.name();
  }

  ShaderCallManifest buildColorCallExpr(const MangledUniforms& uniforms,
                                        const MangledVaryings& varyings) const override {
    ShaderCallManifest result;
    result.outputVarName = "gpColor";
    if (hasColors) {
      result.statement = "vec4 gpColor = " + varyings.get("Color") + ";\n";
    } else {
      result.statement = "vec4 gpColor = " + uniforms.get("Color") + ";\n";
    }
    return result;
  }

  ShaderCallManifest buildCoverageCallExpr(const MangledUniforms& /*uniforms*/,
                                           const MangledVaryings& varyings) const override {
    ShaderCallManifest result;
    result.outputVarName = "gpCoverage";
    if (hasCoverage) {
      result.statement = "vec4 gpCoverage = vec4(" + varyings.get("Coverage") + ");\n";
    } else {
      result.statement = "vec4 gpCoverage = vec4(1.0);\n";
    }
    return result;
  }

  Attribute position = {};
  Attribute texCoord = {};
  Attribute color = {};
  Attribute coverage = {};

  bool hasTexCoords = false;
  bool hasColors = false;
  bool hasCoverage = false;
  PMColor commonColor = {};
  Matrix viewMatrix = {};
};

}  // namespace tgfx

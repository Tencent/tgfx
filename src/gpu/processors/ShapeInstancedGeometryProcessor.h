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

#include "GeometryProcessor.h"
#include "gpu/AAType.h"

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

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  ShapeInstancedGeometryProcessor(int width, int height, AAType aa, bool hasColors,
                                  const Matrix& uvMatrix, const Matrix& viewMatrix);

  void onComputeProcessorKey(BytesKey* bytesKey) const override;

  void onBuildShaderMacros(ShaderMacroSet& macros) const override {
    if (aa == AAType::Coverage) {
      macros.define("TGFX_GP_SHAPE_COVERAGE_AA");
    }
    if (hasColors) {
      macros.define("TGFX_GP_SHAPE_VERTEX_COLORS");
    }
  }

  std::string shaderFunctionFile() const override {
    return "geometry/shape_instanced_geometry.vert";
  }

  ShaderCallResult buildColorCallExpr(const MangledUniforms& /*uniforms*/,
                                      const MangledVaryings& varyings) const override {
    ShaderCallResult result;
    result.outputVarName = "gpColor";
    if (hasColors) {
      result.statement = "vec4 gpColor = " + varyings.get("InstanceColor") + ";\n";
    } else {
      result.statement = "vec4 gpColor = vec4(1.0);\n";
    }
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

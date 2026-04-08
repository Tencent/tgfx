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

#include "gpu/processors/FragmentProcessor.h"

namespace tgfx {
class ConicGradientLayout : public FragmentProcessor {
 public:
  static PlacementPtr<ConicGradientLayout> Make(BlockAllocator* allocator, Matrix matrix,
                                                float bias, float scale);

  std::string name() const override {
    return "ConicGradientLayout";
  }

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  ConicGradientLayout(Matrix matrix, float bias, float scale);

  void onComputeProcessorKey(BytesKey* bytesKey) const override;

  void onBuildShaderMacros(ShaderMacroSet& macros) const override {
    if (coordTransform.matrix.hasPerspective()) {
      macros.define("TGFX_CGRAD_PERSPECTIVE");
    }
  }

  std::string shaderFunctionFile() const override {
    return "fragment/conic_gradient.frag";
  }

  void declareResources(UniformHandler* uniformHandler, MangledUniforms& uniforms,
                        MangledSamplers& /*samplers*/) const override {
    auto biasName = uniformHandler->addUniform("Bias", UniformFormat::Float, ShaderStage::Fragment);
    uniforms.add("Bias", biasName);
    auto scaleName =
        uniformHandler->addUniform("Scale", UniformFormat::Float, ShaderStage::Fragment);
    uniforms.add("Scale", scaleName);
  }

  ShaderCallResult buildCallStatement(const std::string& /*inputColorVar*/, int fpIndex,
                                      const MangledUniforms& uniforms,
                                      const MangledVaryings& varyings,
                                      const MangledSamplers& /*samplers*/) const override {
    ShaderCallResult result;
    result.outputVarName = "color_fp" + std::to_string(fpIndex);
    result.includeFiles = {shaderFunctionFile()};
    auto coord = varyings.getCoordTransform(0);
    result.statement = "vec4 " + result.outputVarName + " = FP_ConicGradientLayout(" + coord +
                       ", " + uniforms.get("Bias") + ", " + uniforms.get("Scale") + ");";
    return result;
  }

  CoordTransform coordTransform;
  float bias;
  float scale;
};
}  // namespace tgfx

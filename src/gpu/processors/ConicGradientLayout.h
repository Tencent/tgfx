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

#include <vector>
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/variants/ShaderVariant.h"

namespace tgfx {
class ConicGradientLayout : public FragmentProcessor {
 public:
  static PlacementPtr<ConicGradientLayout> Make(BlockAllocator* allocator, Matrix matrix,
                                                float bias, float scale);

  std::string name() const override {
    return "ConicGradientLayout";
  }

  static void BuildMacros(bool hasPerspective, ShaderMacroSet& macros);
  static std::vector<ShaderVariant> EnumerateVariants();

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  ConicGradientLayout(Matrix matrix, float bias, float scale);

  void onComputeProcessorKey(BytesKey* bytesKey) const override;

  void onBuildShaderMacros(ShaderMacroSet& macros) const override {
    BuildMacros(coordTransform.matrix.hasPerspective(), macros);
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

  ShaderCallManifest buildCallStatement(const std::string& /*inputColorVar*/, int fpIndex,
                                        const MangledUniforms& uniforms,
                                        const MangledVaryings& varyings,
                                        const MangledSamplers& /*samplers*/) const override {
    ShaderCallManifest manifest;
    manifest.functionName = "TGFX_ConicGradientLayout";
    manifest.outputVarName = "color_fp" + std::to_string(fpIndex);
    manifest.includeFiles = {shaderFunctionFile()};
    manifest.argExpressions = {varyings.getCoordTransform(0), uniforms.get("Bias"),
                               uniforms.get("Scale")};
    return manifest;
  }

  CoordTransform coordTransform;
  float bias;
  float scale;
};
}  // namespace tgfx

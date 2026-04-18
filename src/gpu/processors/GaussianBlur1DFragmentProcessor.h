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

enum class GaussianBlurDirection { Horizontal, Vertical };

class GaussianBlur1DFragmentProcessor : public FragmentProcessor {
 public:
  static PlacementPtr<FragmentProcessor> Make(BlockAllocator* allocator,
                                              PlacementPtr<FragmentProcessor> processor,
                                              float sigma, GaussianBlurDirection direction,
                                              float stepLength, int maxSigma);

  std::string name() const override {
    return "GaussianBlur1DFragmentProcessor";
  }

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  GaussianBlur1DFragmentProcessor(PlacementPtr<FragmentProcessor> processor, float sigma,
                                  GaussianBlurDirection direction, float stepLength, int maxSigma);

  void onComputeProcessorKey(BytesKey*) const override;

  void onBuildShaderMacros(ShaderMacroSet& macros) const override {
    macros.define("TGFX_BLUR_LOOP_LIMIT", 4 * maxSigma);
  }

  std::string shaderFunctionFile() const override {
    return "fragment/gaussian_blur_1d.frag";
  }

  std::vector<ChildEmitInfo> getChildEmitPlan(const std::string& /*parentInput*/) const override {
    return {{0, "vec4(1.0)", -1}};
  }

  void declareResources(UniformHandler* uniformHandler, MangledUniforms& uniforms,
                        MangledSamplers& /*samplers*/) const override {
    uniforms.add("Sigma",
                 uniformHandler->addUniform("Sigma", UniformFormat::Float, ShaderStage::Fragment));
    uniforms.add("Step",
                 uniformHandler->addUniform("Step", UniformFormat::Float2, ShaderStage::Fragment));
  }

  ShaderCallManifest buildContainerCallStatement(const std::string& /*inputColor*/,
                                               const std::vector<std::string>& /*childOutputs*/,
                                               const MangledUniforms& uniforms,
                                               const MangledSamplers& samplers,
                                               const MangledVaryings& varyings) const override {
    // Build TGFX_GB1D_SAMPLE(coord) macro that calls the child FP's texture sampling function.
    // The child FP (TextureEffect) function is TGFX_TextureEffect(inputColor, coord, sampler).
    // For the common non-YUV single-sampler case, we construct the call directly.
    auto samplerName = samplers.getByIndex(0);
    auto coordName = varyings.getCoordTransform(0);
    // The macro takes a coord parameter and calls texture() directly with the child's sampler.
    // This avoids needing to know the full child FP function signature.
    std::string sampleMacro =
        "#define TGFX_GB1D_SAMPLE(coord) texture(" + samplerName + ", coord)\n";

    ShaderCallManifest result;
    result.preamble = sampleMacro;
    result.statement = "vec4 _gb1dResult = TGFX_GaussianBlur1D(" + uniforms.get("Sigma") + ", " +
                       uniforms.get("Step") + ", " + coordName + ");\n";
    result.outputVarName = "_gb1dResult";
    return result;
  }

  float sigma = 0.f;
  GaussianBlurDirection direction = GaussianBlurDirection::Horizontal;
  float stepLength = 1.f;
  int maxSigma = 10;
};
}  // namespace tgfx

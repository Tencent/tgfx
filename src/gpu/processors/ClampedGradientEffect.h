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

#include <climits>
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/variants/ShaderVariant.h"
#include "tgfx/core/Color.h"

namespace tgfx {
class ClampedGradientEffect : public FragmentProcessor {
 public:
  static PlacementPtr<ClampedGradientEffect> Make(BlockAllocator* allocator,
                                                  PlacementPtr<FragmentProcessor> colorizer,
                                                  PlacementPtr<FragmentProcessor> gradLayout,
                                                  Color leftBorderColor, Color rightBorderColor);

  std::string name() const override {
    return "ClampedGradientEffect";
  }

  /** Returns the single trivial shader variant (no compile-time macros). */
  static std::vector<ShaderVariant> EnumerateVariants() {
    return MakeTrivialShaderVariantList("ClampedGradientEffect");
  }

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  ClampedGradientEffect(PlacementPtr<FragmentProcessor> colorizer,
                        PlacementPtr<FragmentProcessor> gradLayout, Color leftBorderColor,
                        Color rightBorderColor);

  size_t colorizerIndex = ULONG_MAX;
  size_t gradLayoutIndex = ULONG_MAX;
  Color leftBorderColor;
  Color rightBorderColor;

  std::string shaderFunctionFile() const override {
    return "fragment/clamped_gradient_effect.frag";
  }

  void declareResources(UniformHandler* uniformHandler, MangledUniforms& uniforms,
                        MangledSamplers& /*samplers*/) const override {
    uniforms.add("leftBorderColor",
                 uniformHandler->addUniform("leftBorderColor", UniformFormat::Float4,
                                            ShaderStage::Fragment));
    uniforms.add("rightBorderColor",
                 uniformHandler->addUniform("rightBorderColor", UniformFormat::Float4,
                                            ShaderStage::Fragment));
  }

  std::vector<ChildEmitInfo> getChildEmitPlan(const std::string& /*parentInput*/) const override {
    // Emit gradLayout first (child gradLayoutIndex), then colorizer (child colorizerIndex)
    // using gradLayout's output as colorizer's input.
    return {{gradLayoutIndex, "vec4(1.0)", -1},
            {colorizerIndex, "", static_cast<int>(gradLayoutIndex)}};
  }

  ShaderCallManifest buildContainerCallStatement(
      const std::string& inputColor, const std::vector<std::string>& childOutputs,
      const MangledUniforms& uniforms, const MangledSamplers& /*samplers*/,
      const MangledVaryings& /*varyings*/) const override {
    // childOutputs[gradLayoutIndex] = gradLayout result (t value)
    // childOutputs[colorizerIndex] = colorizer result (color at t)
    ShaderCallManifest manifest;
    manifest.functionName = "TGFX_ClampedGradientEffect";
    manifest.outputVarName = "_cgeResult";
    manifest.argExpressions = {inputColor.empty() ? std::string("vec4(1.0)") : inputColor,
                               childOutputs[gradLayoutIndex], childOutputs[colorizerIndex],
                               uniforms.get("leftBorderColor"), uniforms.get("rightBorderColor")};
    return manifest;
  }
};
}  // namespace tgfx

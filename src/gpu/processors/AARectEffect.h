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
class AARectEffect : public FragmentProcessor {
 public:
  static PlacementPtr<AARectEffect> Make(BlockAllocator* allocator, const Rect& rect);

  std::string name() const override {
    return "AARectEffect";
  }

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  explicit AARectEffect(const Rect& rect) : FragmentProcessor(ClassID()), rect(rect) {
  }

  std::string shaderFunctionFile() const override {
    return "fragment/aa_rect_effect.frag";
  }

  void declareResources(UniformHandler* uniformHandler, MangledUniforms& uniforms,
                        MangledSamplers& /*samplers*/) const override {
    auto rectName =
        uniformHandler->addUniform("Rect", UniformFormat::Float4, ShaderStage::Fragment);
    uniforms.add("Rect", rectName);
  }

  ShaderCallManifest buildCallStatement(const std::string& inputColorVar, int fpIndex,
                                      const MangledUniforms& uniforms,
                                      const MangledVaryings& /*varyings*/,
                                      const MangledSamplers& /*samplers*/) const override {
    ShaderCallManifest result;
    result.outputVarName = "color_fp" + std::to_string(fpIndex);
    result.includeFiles = {shaderFunctionFile()};
    auto input = inputColorVar.empty() ? "vec4(1.0)" : inputColorVar;
    result.statement = "vec4 " + result.outputVarName + " = TGFX_AARectEffect(" + input + ", " +
                       uniforms.get("Rect") + ");";
    return result;
  }

  Rect rect = {};
};
}  // namespace tgfx

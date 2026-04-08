/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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
class LumaFragmentProcessor : public FragmentProcessor {
 public:
  static PlacementPtr<FragmentProcessor> Make(BlockAllocator* allocator,
                                              std::shared_ptr<ColorSpace> colorSpace = nullptr);

  std::string name() const override {
    return "LumaFragmentProcessor";
  }

  void computeProcessorKey(Context* context, BytesKey* bytesKey) const override;

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  LumaFragmentProcessor(std::shared_ptr<ColorSpace> colorSpace);

  std::string shaderFunctionFile() const override {
    return "fragment/luma.frag";
  }

  void declareResources(UniformHandler* uniformHandler, MangledUniforms& uniforms,
                        MangledSamplers& /*samplers*/) const override {
    auto krName = uniformHandler->addUniform("Kr", UniformFormat::Float, ShaderStage::Fragment);
    uniforms.add("Kr", krName);
    auto kgName = uniformHandler->addUniform("Kg", UniformFormat::Float, ShaderStage::Fragment);
    uniforms.add("Kg", kgName);
    auto kbName = uniformHandler->addUniform("Kb", UniformFormat::Float, ShaderStage::Fragment);
    uniforms.add("Kb", kbName);
  }

  ShaderCallResult buildCallStatement(const std::string& inputColorVar, int fpIndex,
                                      const MangledUniforms& uniforms,
                                      const MangledVaryings& /*varyings*/,
                                      const MangledSamplers& /*samplers*/) const override {
    ShaderCallResult result;
    result.outputVarName = "color_fp" + std::to_string(fpIndex);
    result.includeFiles = {shaderFunctionFile()};
    auto input = inputColorVar.empty() ? "vec4(1.0)" : inputColorVar;
    result.statement = "vec4 " + result.outputVarName + " = FP_Luma(" + input + ", " +
                       uniforms.get("Kr") + ", " + uniforms.get("Kg") + ", " + uniforms.get("Kb") +
                       ");";
    return result;
  }

  struct LumaFactor {
    /** default ITU-R Recommendation BT.709 at http://www.itu.int/rec/R-REC-BT.709/ .*/
    float kr = 0.2126f;
    float kg = 0.7152f;
    float kb = 0.0722f;
  };

  std::shared_ptr<ColorSpace> _colorSpace = nullptr;
  LumaFactor _lumaFactor;

 private:
  static LumaFactor AcquireLumaFactorFromColorSpace(const ColorMatrix33& matrix);
};
}  // namespace tgfx

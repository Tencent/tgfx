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
#include "tgfx/core/Color.h"

namespace tgfx {
enum class InputMode { Ignore = 0, ModulateRGBA = 1, ModulateA = 2 };

class ConstColorProcessor : public FragmentProcessor {
 public:
  static PlacementPtr<ConstColorProcessor> Make(BlockAllocator* allocator, PMColor color,
                                                InputMode inputMode);

  std::string name() const override {
    return "ConstColorProcessor";
  }

  void onComputeProcessorKey(BytesKey* bytesKey) const override;

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  ConstColorProcessor(PMColor color, InputMode mode)
      : FragmentProcessor(ClassID()), color(color), inputMode(mode) {
  }

  PMColor color;
  InputMode inputMode;

  void onBuildShaderMacros(ShaderMacroSet& /*macros*/) const override {
  }

  std::string shaderFunctionFile() const override {
    return "fragment/const_color.frag";
  }

  void declareResources(UniformHandler* uniformHandler, MangledUniforms& uniforms,
                        MangledSamplers& /*samplers*/) const override {
    auto colorName =
        uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);
    uniforms.add("Color", colorName);
  }

  ShaderCallManifest buildCallStatement(const std::string& inputColorVar, int fpIndex,
                                      const MangledUniforms& uniforms,
                                      const MangledVaryings& /*varyings*/,
                                      const MangledSamplers& /*samplers*/) const override {
    ShaderCallManifest result;
    result.outputVarName = "color_fp" + std::to_string(fpIndex);
    result.includeFiles = {shaderFunctionFile()};
    auto input = inputColorVar.empty() ? "vec4(1.0)" : inputColorVar;
    result.statement = "vec4 " + result.outputVarName + " = TGFX_ConstColor(" + input + ", " +
                       uniforms.get("Color") + ", " + std::to_string(static_cast<int>(inputMode)) +
                       ");";
    return result;
  }
};
}  // namespace tgfx

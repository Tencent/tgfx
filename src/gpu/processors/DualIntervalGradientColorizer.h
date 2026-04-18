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
class DualIntervalGradientColorizer : public FragmentProcessor {
 public:
  static PlacementPtr<DualIntervalGradientColorizer> Make(BlockAllocator* allocator, Color c0,
                                                          Color c1, Color c2, Color c3,
                                                          float threshold);

  std::string name() const override {
    return "DualIntervalGradientColorizer";
  }

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  DualIntervalGradientColorizer(Color scale01, Color bias01, Color scale23, Color bias23,
                                float threshold)
      : FragmentProcessor(ClassID()), scale01(scale01), bias01(bias01), scale23(scale23),
        bias23(bias23), threshold(threshold) {
  }

  std::string shaderFunctionFile() const override {
    return "fragment/dual_interval_gradient.frag";
  }

  void declareResources(UniformHandler* uniformHandler, MangledUniforms& uniforms,
                        MangledSamplers& /*samplers*/) const override {
    auto scale01Name =
        uniformHandler->addUniform("scale01", UniformFormat::Float4, ShaderStage::Fragment);
    uniforms.add("scale01", scale01Name);
    auto bias01Name =
        uniformHandler->addUniform("bias01", UniformFormat::Float4, ShaderStage::Fragment);
    uniforms.add("bias01", bias01Name);
    auto scale23Name =
        uniformHandler->addUniform("scale23", UniformFormat::Float4, ShaderStage::Fragment);
    uniforms.add("scale23", scale23Name);
    auto bias23Name =
        uniformHandler->addUniform("bias23", UniformFormat::Float4, ShaderStage::Fragment);
    uniforms.add("bias23", bias23Name);
    auto thresholdName =
        uniformHandler->addUniform("threshold", UniformFormat::Float, ShaderStage::Fragment);
    uniforms.add("threshold", thresholdName);
  }

  ShaderCallManifest buildCallStatement(const std::string& inputColorVar, int fpIndex,
                                        const MangledUniforms& uniforms,
                                        const MangledVaryings& /*varyings*/,
                                        const MangledSamplers& /*samplers*/) const override {
    ShaderCallManifest manifest;
    manifest.functionName = "TGFX_DualIntervalGradientColorizer";
    manifest.outputVarName = "color_fp" + std::to_string(fpIndex);
    manifest.includeFiles = {shaderFunctionFile()};
    manifest.argExpressions = {inputColorVar.empty() ? std::string("vec4(1.0)") : inputColorVar,
                               uniforms.get("scale01"),
                               uniforms.get("bias01"),
                               uniforms.get("scale23"),
                               uniforms.get("bias23"),
                               uniforms.get("threshold")};
    return manifest;
  }

  Color scale01;
  Color bias01;
  Color scale23;
  Color bias23;
  float threshold;
};
}  // namespace tgfx

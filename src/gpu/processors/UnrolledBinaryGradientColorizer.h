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
class UnrolledBinaryGradientColorizer : public FragmentProcessor {
 public:
  static constexpr int MaxColorCount = 16;

  static PlacementPtr<UnrolledBinaryGradientColorizer> Make(BlockAllocator* allocator,
                                                            const Color* colors,
                                                            const float* positions, int count);

  std::string name() const override {
    return "UnrolledBinaryGradientColorizer";
  }

  void onComputeProcessorKey(BytesKey* bytesKey) const override;

  void onBuildShaderMacros(ShaderMacroSet& macros) const override {
    macros.define("TGFX_UBGC_INTERVAL_COUNT", intervalCount);
  }

  std::string shaderFunctionFile() const override {
    return "fragment/unrolled_binary_gradient.frag";
  }

  void declareResources(UniformHandler* uniformHandler, MangledUniforms& uniforms,
                        MangledSamplers& /*samplers*/) const override {
    addScaleBiasUniform(uniformHandler, uniforms, "scale0_1", 0);
    addScaleBiasUniform(uniformHandler, uniforms, "scale2_3", 1);
    addScaleBiasUniform(uniformHandler, uniforms, "scale4_5", 2);
    addScaleBiasUniform(uniformHandler, uniforms, "scale6_7", 3);
    addScaleBiasUniform(uniformHandler, uniforms, "scale8_9", 4);
    addScaleBiasUniform(uniformHandler, uniforms, "scale10_11", 5);
    addScaleBiasUniform(uniformHandler, uniforms, "scale12_13", 6);
    addScaleBiasUniform(uniformHandler, uniforms, "scale14_15", 7);
    addScaleBiasUniform(uniformHandler, uniforms, "bias0_1", 0);
    addScaleBiasUniform(uniformHandler, uniforms, "bias2_3", 1);
    addScaleBiasUniform(uniformHandler, uniforms, "bias4_5", 2);
    addScaleBiasUniform(uniformHandler, uniforms, "bias6_7", 3);
    addScaleBiasUniform(uniformHandler, uniforms, "bias8_9", 4);
    addScaleBiasUniform(uniformHandler, uniforms, "bias10_11", 5);
    addScaleBiasUniform(uniformHandler, uniforms, "bias12_13", 6);
    addScaleBiasUniform(uniformHandler, uniforms, "bias14_15", 7);
    auto t17 =
        uniformHandler->addUniform("thresholds1_7", UniformFormat::Float4, ShaderStage::Fragment);
    uniforms.add("thresholds1_7", t17);
    auto t913 =
        uniformHandler->addUniform("thresholds9_13", UniformFormat::Float4, ShaderStage::Fragment);
    uniforms.add("thresholds9_13", t913);
  }

  ShaderCallManifest buildCallStatement(const std::string& inputColorVar, int fpIndex,
                                        const MangledUniforms& uniforms,
                                        const MangledVaryings& /*varyings*/,
                                        const MangledSamplers& /*samplers*/) const override {
    ShaderCallManifest manifest;
    manifest.functionName = "TGFX_UnrolledBinaryGradientColorizer";
    manifest.outputVarName = "color_fp" + std::to_string(fpIndex);
    manifest.includeFiles = {shaderFunctionFile()};
    manifest.argExpressions.push_back(inputColorVar.empty() ? std::string("vec4(1.0)")
                                                            : inputColorVar);
    manifest.argExpressions.push_back(uniforms.get("scale0_1"));
    manifest.argExpressions.push_back(uniforms.get("bias0_1"));
    manifest.argExpressions.push_back(uniforms.get("thresholds1_7"));
    if (intervalCount > 1) {
      manifest.argExpressions.push_back(uniforms.get("scale2_3"));
      manifest.argExpressions.push_back(uniforms.get("bias2_3"));
    }
    if (intervalCount > 2) {
      manifest.argExpressions.push_back(uniforms.get("scale4_5"));
      manifest.argExpressions.push_back(uniforms.get("bias4_5"));
    }
    if (intervalCount > 3) {
      manifest.argExpressions.push_back(uniforms.get("scale6_7"));
      manifest.argExpressions.push_back(uniforms.get("bias6_7"));
    }
    if (intervalCount > 4) {
      manifest.argExpressions.push_back(uniforms.get("scale8_9"));
      manifest.argExpressions.push_back(uniforms.get("bias8_9"));
      manifest.argExpressions.push_back(uniforms.get("thresholds9_13"));
    }
    if (intervalCount > 5) {
      manifest.argExpressions.push_back(uniforms.get("scale10_11"));
      manifest.argExpressions.push_back(uniforms.get("bias10_11"));
    }
    if (intervalCount > 6) {
      manifest.argExpressions.push_back(uniforms.get("scale12_13"));
      manifest.argExpressions.push_back(uniforms.get("bias12_13"));
    }
    if (intervalCount > 7) {
      manifest.argExpressions.push_back(uniforms.get("scale14_15"));
      manifest.argExpressions.push_back(uniforms.get("bias14_15"));
    }
    return manifest;
  }

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  UnrolledBinaryGradientColorizer(int intervalCount, Color* scales, Color* biases,
                                  Rect thresholds1_7, Rect thresholds9_13)
      : FragmentProcessor(ClassID()), intervalCount(intervalCount), scale0_1(scales[0]),
        scale2_3(scales[1]), scale4_5(scales[2]), scale6_7(scales[3]), scale8_9(scales[4]),
        scale10_11(scales[5]), scale12_13(scales[6]), scale14_15(scales[7]), bias0_1(biases[0]),
        bias2_3(biases[1]), bias4_5(biases[2]), bias6_7(biases[3]), bias8_9(biases[4]),
        bias10_11(biases[5]), bias12_13(biases[6]), bias14_15(biases[7]),
        thresholds1_7(thresholds1_7), thresholds9_13(thresholds9_13) {
  }

  int intervalCount;
  Color scale0_1;
  Color scale2_3;
  Color scale4_5;
  Color scale6_7;
  Color scale8_9;
  Color scale10_11;
  Color scale12_13;
  Color scale14_15;
  Color bias0_1;
  Color bias2_3;
  Color bias4_5;
  Color bias6_7;
  Color bias8_9;
  Color bias10_11;
  Color bias12_13;
  Color bias14_15;
  Rect thresholds1_7;
  Rect thresholds9_13;

  void addScaleBiasUniform(UniformHandler* uniformHandler, MangledUniforms& uniforms,
                           const std::string& uniformName, int limit) const {
    if (intervalCount > limit) {
      auto mangledName =
          uniformHandler->addUniform(uniformName, UniformFormat::Float4, ShaderStage::Fragment);
      uniforms.add(uniformName, mangledName);
    }
  }
};
}  // namespace tgfx

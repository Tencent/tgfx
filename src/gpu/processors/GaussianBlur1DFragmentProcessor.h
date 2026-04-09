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

  bool emitContainerCode(FragmentShaderBuilder* fragBuilder, UniformHandler* uniformHandler,
                         const std::string& /*input*/, const std::string& output,
                         size_t transformedCoordVarsIdx,
                         const EmitChildFunc& emitChild) const override {
    auto sigmaName =
        uniformHandler->addUniform("Sigma", UniformFormat::Float, ShaderStage::Fragment);
    auto texelSizeName =
        uniformHandler->addUniform("Step", UniformFormat::Float2, ShaderStage::Fragment);
    fragBuilder->codeAppendf("vec2 offset = %s;", texelSizeName.c_str());
    fragBuilder->codeAppendf("float sigma = %s;", sigmaName.c_str());
    fragBuilder->codeAppend("int radius = int(ceil(2.0 * sigma));");
    fragBuilder->codeAppend("vec4 sum = vec4(0.0);");
    fragBuilder->codeAppend("float total = 0.0;");
    fragBuilder->codeAppendf("for (int j = 0; j <= %d; ++j) {", 4 * maxSigma);
    fragBuilder->codeAppend("int i = j - radius;");
    fragBuilder->codeAppend("float weight = exp(-float(i*i) / (2.0*sigma*sigma));");
    fragBuilder->codeAppend("total += weight;");
    auto childCoordIdx = computeChildCoordOffset(transformedCoordVarsIdx, 0);
    auto childOutput =
        emitChild(childProcessor(0), childCoordIdx, "vec4(1.0)",
                  [](const std::string& coord) { return "(" + coord + " + offset * float(i))"; });
    fragBuilder->codeAppendf("sum += %s * weight;", childOutput.c_str());
    fragBuilder->codeAppend("if (i == radius) { break; }");
    fragBuilder->codeAppend("}");
    fragBuilder->codeAppendf("%s = sum / total;", output.c_str());
    return true;
  }

  float sigma = 0.f;
  GaussianBlurDirection direction = GaussianBlurDirection::Horizontal;
  float stepLength = 1.f;
  int maxSigma = 10;

  friend class ModularProgramBuilder;
};
}  // namespace tgfx

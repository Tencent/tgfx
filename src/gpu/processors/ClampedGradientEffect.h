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

  bool emitContainerCode(FragmentShaderBuilder* fragBuilder, UniformHandler* uniformHandler,
                         const std::string& input, const std::string& output,
                         size_t transformedCoordVarsIdx,
                         const EmitChildFunc& emitChild) const override {
    auto leftName =
        uniformHandler->addUniform("leftBorderColor", UniformFormat::Float4, ShaderStage::Fragment);
    auto rightName = uniformHandler->addUniform("rightBorderColor", UniformFormat::Float4,
                                                ShaderStage::Fragment);
    auto gradCoordIdx = computeChildCoordOffset(transformedCoordVarsIdx, gradLayoutIndex);
    auto tVar = emitChild(childProcessor(gradLayoutIndex), gradCoordIdx, "vec4(1.0)", {});
    fragBuilder->codeAppendf("vec4 t = %s;", tVar.c_str());
    fragBuilder->codeAppend("if (t.y < 0.0) {");
    fragBuilder->codeAppendf("%s = vec4(0.0);", output.c_str());
    fragBuilder->codeAppend("} else if (t.x <= 0.0) {");
    fragBuilder->codeAppendf("%s = %s;", output.c_str(), leftName.c_str());
    fragBuilder->codeAppend("} else if (t.x >= 1.0) {");
    fragBuilder->codeAppendf("%s = %s;", output.c_str(), rightName.c_str());
    fragBuilder->codeAppend("} else {");
    auto colorizerCoordIdx = computeChildCoordOffset(transformedCoordVarsIdx, colorizerIndex);
    auto colorVar = emitChild(childProcessor(colorizerIndex), colorizerCoordIdx, "t", {});
    fragBuilder->codeAppendf("%s = %s;", output.c_str(), colorVar.c_str());
    fragBuilder->codeAppend("}");
    fragBuilder->codeAppendf("%s.rgb *= %s.a;", output.c_str(), output.c_str());
    fragBuilder->codeAppendf("%s *= %s.a;", output.c_str(),
                             input.empty() ? "vec4(1.0)" : input.c_str());
    return true;
  }
};
}  // namespace tgfx

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

#include "GLDualIntervalGradientColorizer.h"

namespace tgfx {
PlacementPtr<DualIntervalGradientColorizer> DualIntervalGradientColorizer::Make(BlockBuffer* buffer,
                                                                                Color c0, Color c1,
                                                                                Color c2, Color c3,
                                                                                float threshold) {
  Color scale01;
  // Derive scale and biases from the 4 colors and threshold
  for (int i = 0; i < 4; ++i) {
    auto vc0 = c0[i];
    auto vc1 = c1[i];
    scale01[i] = (vc1 - vc0) / threshold;
  }
  // bias01 = c0

  Color scale23;
  Color bias23;
  for (int i = 0; i < 4; ++i) {
    auto vc2 = c2[i];
    auto vc3 = c3[i];
    scale23[i] = (vc3 - vc2) / (1 - threshold);
    bias23[i] = vc2 - threshold * scale23[i];
  }

  return buffer->make<GLDualIntervalGradientColorizer>(scale01, c0, scale23, bias23, threshold);
}

GLDualIntervalGradientColorizer::GLDualIntervalGradientColorizer(Color scale01, Color bias01,
                                                                 Color scale23, Color bias23,
                                                                 float threshold)
    : DualIntervalGradientColorizer(scale01, bias01, scale23, bias23, threshold) {
}

void GLDualIntervalGradientColorizer::emitCode(EmitArgs& args) const {
  auto* fragBuilder = args.fragBuilder;
  auto scale01Name =
      args.uniformHandler->addUniform(ShaderFlags::Fragment, SLType::Float4, "scale01");
  auto bias01Name =
      args.uniformHandler->addUniform(ShaderFlags::Fragment, SLType::Float4, "bias01");
  auto scale23Name =
      args.uniformHandler->addUniform(ShaderFlags::Fragment, SLType::Float4, "scale23");
  auto bias23Name =
      args.uniformHandler->addUniform(ShaderFlags::Fragment, SLType::Float4, "bias23");
  auto thresholdName =
      args.uniformHandler->addUniform(ShaderFlags::Fragment, SLType::Float, "threshold");
  fragBuilder->codeAppendf("float t = %s.x;", args.inputColor.c_str());
  fragBuilder->codeAppend("vec4 scale, bias;");
  fragBuilder->codeAppendf("if (t < %s) {", thresholdName.c_str());
  fragBuilder->codeAppendf("scale = %s;", scale01Name.c_str());
  fragBuilder->codeAppendf("bias = %s;", bias01Name.c_str());
  fragBuilder->codeAppend("} else {");
  fragBuilder->codeAppendf("scale = %s;", scale23Name.c_str());
  fragBuilder->codeAppendf("bias = %s;", bias23Name.c_str());
  fragBuilder->codeAppend("}");
  fragBuilder->codeAppendf("%s = vec4(t * scale + bias);", args.outputColor.c_str());
}

void GLDualIntervalGradientColorizer::onSetData(UniformBuffer* uniformBuffer) const {
  uniformBuffer->setData("scale01", scale01);
  uniformBuffer->setData("bias01", bias01);
  uniformBuffer->setData("scale23", scale23);
  uniformBuffer->setData("bias23", bias23);
  uniformBuffer->setData("threshold", threshold);
}
}  // namespace tgfx

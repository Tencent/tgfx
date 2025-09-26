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

#include "GLSLClampedGradientEffect.h"

namespace tgfx {
PlacementPtr<ClampedGradientEffect> ClampedGradientEffect::Make(
    BlockBuffer* buffer, PlacementPtr<FragmentProcessor> colorizer,
    PlacementPtr<FragmentProcessor> gradLayout, Color leftBorderColor, Color rightBorderColor) {
  return buffer->make<GLSLClampedGradientEffect>(std::move(colorizer), std::move(gradLayout),
                                                 leftBorderColor, rightBorderColor);
}

GLSLClampedGradientEffect::GLSLClampedGradientEffect(PlacementPtr<FragmentProcessor> colorizer,
                                                     PlacementPtr<FragmentProcessor> gradLayout,
                                                     Color leftBorderColor, Color rightBorderColor)
    : ClampedGradientEffect(std::move(colorizer), std::move(gradLayout), leftBorderColor,
                            rightBorderColor) {
}

void GLSLClampedGradientEffect::emitCode(EmitArgs& args) const {
  auto fragBuilder = args.fragBuilder;
  auto leftBorderColorName = args.uniformHandler->addUniform(
      "leftBorderColor", UniformFormat::Float4, ShaderStage::Fragment);
  auto rightBorderColorName = args.uniformHandler->addUniform(
      "rightBorderColor", UniformFormat::Float4, ShaderStage::Fragment);
  std::string _child1 = "_child1";
  emitChild(gradLayoutIndex, &_child1, args);
  fragBuilder->codeAppendf("vec4 t = %s;", _child1.c_str());
  fragBuilder->codeAppend("if (t.y < 0.0) {");
  fragBuilder->codeAppendf("%s = vec4(0.0);", args.outputColor.c_str());
  fragBuilder->codeAppend("} else if (t.x <= 0.0) {");
  fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), leftBorderColorName.c_str());
  fragBuilder->codeAppend("} else if (t.x >= 1.0) {");
  fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), rightBorderColorName.c_str());
  fragBuilder->codeAppend("} else {");
  std::string _input0 = "t";
  std::string _child0 = "_child0";
  emitChild(colorizerIndex, _input0, &_child0, args);
  fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), _child0.c_str());
  fragBuilder->codeAppend("}");
  // make sure the output color is premultiplied
  fragBuilder->codeAppendf("%s.rgb *= %s.a;", args.outputColor.c_str(), args.outputColor.c_str());
  fragBuilder->codeAppendf("%s *= %s.a;", args.outputColor.c_str(), args.inputColor.c_str());
}

void GLSLClampedGradientEffect::onSetData(UniformData* /*vertexUniformData*/,
                                          UniformData* fragmentUniformData) const {
  fragmentUniformData->setData("leftBorderColor", leftBorderColor);
  fragmentUniformData->setData("rightBorderColor", rightBorderColor);
}
}  // namespace tgfx

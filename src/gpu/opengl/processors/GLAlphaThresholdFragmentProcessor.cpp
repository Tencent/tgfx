/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "GLAlphaThresholdFragmentProcessor.h"

namespace tgfx {
void GLAlphaThresholdFragmentProcessor::emitCode(EmitArgs& args) const {
  auto* uniformHandler = args.uniformHandler;
  auto thresholdUniformName =
      uniformHandler->addUniform(ShaderFlags::Fragment, SLType::Float, "Threshold");

  auto* fragBuilder = args.fragBuilder;
  fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), args.inputColor.c_str());
  fragBuilder->codeAppendf("if (%s.a > %s)", args.outputColor.c_str(),
                           thresholdUniformName.c_str());
  fragBuilder->codeAppend("{");
  fragBuilder->codeAppendf("%s.rgb /= %s.a;", args.outputColor.c_str(), args.outputColor.c_str());
  fragBuilder->codeAppendf("%s.a = 1.0;", args.outputColor.c_str());
  fragBuilder->codeAppend("}");
  fragBuilder->codeAppend("else");
  fragBuilder->codeAppend("{");
  fragBuilder->codeAppendf("%s = vec4(0.0);", args.outputColor.c_str());
  fragBuilder->codeAppend("}");
  fragBuilder->codeAppendf("%s = clamp(%s, 0.0, 1.0);", args.outputColor.c_str(),
                           args.outputColor.c_str());
}

void GLAlphaThresholdFragmentProcessor::onSetData(UniformBuffer* uniformBuffer) const {
  uniformBuffer->setData("Threshold", threshold);
}

}  // namespace tgfx

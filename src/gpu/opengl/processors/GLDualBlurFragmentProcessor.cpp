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

#include "GLDualBlurFragmentProcessor.h"
#include "core/utils/Log.h"

namespace tgfx {
PlacementPtr<DualBlurFragmentProcessor> DualBlurFragmentProcessor::Make(
    BlockBuffer* buffer, DualBlurPassMode passMode, PlacementPtr<FragmentProcessor> processor,
    Point blurOffset) {
  if (processor == nullptr) {
    return nullptr;
  }
  return buffer->make<GLDualBlurFragmentProcessor>(passMode, std::move(processor), blurOffset);
}

GLDualBlurFragmentProcessor::GLDualBlurFragmentProcessor(DualBlurPassMode passMode,
                                                         PlacementPtr<FragmentProcessor> processor,
                                                         Point blurOffset)
    : DualBlurFragmentProcessor(passMode, std::move(processor), blurOffset) {
}

void GLDualBlurFragmentProcessor::emitCode(EmitArgs& args) const {
  auto* fragBuilder = args.fragBuilder;
  auto blurOffsetName =
      args.uniformHandler->addUniform(ShaderFlags::Fragment, SLType::Float2, "Blur");
  auto stepName = args.uniformHandler->addUniform(ShaderFlags::Fragment, SLType::Float2, "Step");
  std::string tempColor = "tempColor";
  if (passMode == DualBlurPassMode::Down) {
    fragBuilder->codeAppend("const int size = 5;");
    fragBuilder->codeAppendf("vec2 coords[size];");
    fragBuilder->codeAppend("coords[0] = vec2(0.0, 0.0);");
    fragBuilder->codeAppendf("coords[1] = -%s * %s;", stepName.c_str(), blurOffsetName.c_str());
    fragBuilder->codeAppendf("coords[2] = %s * %s;", stepName.c_str(), blurOffsetName.c_str());
    fragBuilder->codeAppendf("coords[3] = vec2(%s.x, -%s.y) * %s;", stepName.c_str(),
                             stepName.c_str(), blurOffsetName.c_str());
    fragBuilder->codeAppendf("coords[4] = -vec2(%s.x, -%s.y) * %s;", stepName.c_str(),
                             stepName.c_str(), blurOffsetName.c_str());
    fragBuilder->codeAppendf("vec4 sum;");
    fragBuilder->codeAppend("for (int i = 0; i < size; i++) {");
    emitChild(0, &tempColor, args,
              [](std::string_view coord) { return std::string(coord) + " + coords[i]"; });
    fragBuilder->codeAppend("if (i == 0) {");
    fragBuilder->codeAppendf("sum = %s * 4.0;", tempColor.c_str());
    fragBuilder->codeAppend("} else {");
    fragBuilder->codeAppendf("sum += %s;", tempColor.c_str());
    fragBuilder->codeAppend("}");
    fragBuilder->codeAppend("}");
    fragBuilder->codeAppendf("%s = sum / 8.0;", args.outputColor.c_str());
  } else {
    fragBuilder->codeAppend("const int size = 8;");
    fragBuilder->codeAppend("vec2 coords[size];");
    fragBuilder->codeAppendf("coords[0] = vec2(-%s.x * 2.0, 0.0) * %s;", stepName.c_str(),
                             blurOffsetName.c_str());
    fragBuilder->codeAppendf("coords[1] = vec2(-%s.x, %s.y) * %s;", stepName.c_str(),
                             stepName.c_str(), blurOffsetName.c_str());
    fragBuilder->codeAppendf("coords[2] = vec2(0.0, %s.y * 2.0) * %s;", stepName.c_str(),
                             blurOffsetName.c_str());
    fragBuilder->codeAppendf("coords[3] = %s * %s;", stepName.c_str(), blurOffsetName.c_str());
    fragBuilder->codeAppendf("coords[4] = vec2(%s.x * 2.0, 0.0) * %s;", stepName.c_str(),
                             blurOffsetName.c_str());
    fragBuilder->codeAppendf("coords[5] = vec2(%s.x, -%s.y) * %s;", stepName.c_str(),
                             stepName.c_str(), blurOffsetName.c_str());
    fragBuilder->codeAppendf("coords[6] = vec2(0.0, -%s.y * 2.0) * %s;", stepName.c_str(),
                             blurOffsetName.c_str());
    fragBuilder->codeAppendf("coords[7] = vec2(-%s.x, -%s.y) * %s;", stepName.c_str(),
                             stepName.c_str(), blurOffsetName.c_str());
    fragBuilder->codeAppend("vec4 sum = vec4(0.0);");
    fragBuilder->codeAppend("for (int i = 0; i < size; i++) {");
    emitChild(0, &tempColor, args,
              [](std::string_view coord) { return std::string(coord) + " + coords[i]"; });
    fragBuilder->codeAppend("if (mod(float(i), 2.0) == 0.0) {");
    fragBuilder->codeAppendf("sum += %s;", tempColor.c_str());
    fragBuilder->codeAppend("} else {");
    fragBuilder->codeAppendf("sum += %s * 2.0;", tempColor.c_str());
    fragBuilder->codeAppend("}");
    fragBuilder->codeAppend("}");
    fragBuilder->codeAppendf("%s = sum / 12.0;", args.outputColor.c_str());
  }
}

void GLDualBlurFragmentProcessor::onSetData(UniformBuffer* uniformBuffer) const {
  auto* processor = childProcessor(0);
  Point stepVectors[] = {{0, 0}, {0.5, 0.5}};
  if (processor->numCoordTransforms() > 0) {
    auto transform = processor->coordTransform(0);
    auto matrix = transform->getTotalMatrix();
    matrix.mapPoints(stepVectors, 2);
  }
  Point step = stepVectors[1] - stepVectors[0];
  uniformBuffer->setData("Blur", blurOffset);
  uniformBuffer->setData("Step", step);
}
}  // namespace tgfx

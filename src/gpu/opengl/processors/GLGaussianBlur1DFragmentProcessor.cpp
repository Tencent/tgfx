/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "GLGaussianBlur1DFragmentProcessor.h"

namespace tgfx {

std::unique_ptr<GaussianBlur1DFragmentProcessor> GaussianBlur1DFragmentProcessor::Make(
    std::unique_ptr<FragmentProcessor> processor, float sigma, GaussianBlurDirection direction,
    float stepLength) {
  if (!processor) {
    return nullptr;
  }

  return std::unique_ptr<GaussianBlur1DFragmentProcessor>(
      new GLGaussianBlur1DFragmentProcessor(std::move(processor), sigma, direction, stepLength));
}

GLGaussianBlur1DFragmentProcessor::GLGaussianBlur1DFragmentProcessor(
    std::unique_ptr<FragmentProcessor> processor, float sigma, GaussianBlurDirection direction,
    float stepLength)
    : GaussianBlur1DFragmentProcessor(std::move(processor), sigma, direction, stepLength) {
}

void GLGaussianBlur1DFragmentProcessor::emitCode(EmitArgs& args) const {
  auto* fragBuilder = args.fragBuilder;

  std::string sigmaName =
      args.uniformHandler->addUniform(ShaderFlags::Fragment, SLType::Float, "Sigma");
  std::string texelSizeName =
      args.uniformHandler->addUniform(ShaderFlags::Fragment, SLType::Float2, "Step");

  fragBuilder->codeAppendf("vec2 offset = %s;", texelSizeName.c_str());

  fragBuilder->codeAppendf("float sigma = %s;", sigmaName.c_str());
  fragBuilder->codeAppend("int radius = int(ceil(2 * sigma));");
  fragBuilder->codeAppend("vec4 sum = vec4(0.0);");
  fragBuilder->codeAppend("float total = 0.0;");

  fragBuilder->codeAppend("for (int i = -radius; i <= radius; ++i) {");
  fragBuilder->codeAppend("float weight = exp(-float(i*i) / (2.0*sigma*sigma));");
  fragBuilder->codeAppend("total += weight;");

  std::string tempColor = "tempColor";
  emitChild(0, &tempColor, args, [](std::string_view coord) {
    return "(" + std::string(coord) + " + offset * float(i))";
  });

  fragBuilder->codeAppendf("sum += %s * weight;", tempColor.c_str());
  fragBuilder->codeAppend("}");
  fragBuilder->codeAppendf("%s = sum / total;", args.outputColor.c_str());
}

void GLGaussianBlur1DFragmentProcessor::onSetData(UniformBuffer* uniformBuffer) const {
  auto* processor = childProcessor(0);
  Point stepVectors[] = {{0, 0}, {stepLength, 0}};
  if (direction == GaussianBlurDirection::Vertical) {
    stepVectors[1] = {0, stepLength};
  }

  if (auto transform = processor->coordTransform(0)) {
    auto matrix = transform->getTotalMatrix();
    matrix.mapPoints(stepVectors, 2);
  }
  Point step = stepVectors[1] - stepVectors[0];
  uniformBuffer->setData("Sigma", sigma);
  uniformBuffer->setData("Step", step);
}
}  // namespace tgfx

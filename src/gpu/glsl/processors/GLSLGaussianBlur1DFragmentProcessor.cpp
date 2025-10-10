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

#include "GLSLGaussianBlur1DFragmentProcessor.h"

namespace tgfx {

PlacementPtr<FragmentProcessor> GaussianBlur1DFragmentProcessor::Make(
    BlockBuffer* buffer, PlacementPtr<FragmentProcessor> processor, float sigma,
    GaussianBlurDirection direction, float stepLength, int maxSigma) {
  if (!processor) {
    return nullptr;
  }
  if (maxSigma < 0) {
    return nullptr;
  }
  if (sigma <= 0 || stepLength <= 0) {
    return processor;
  }

  return buffer->make<GLSLGaussianBlur1DFragmentProcessor>(
      std::move(processor), sigma, direction, stepLength, static_cast<int>(ceil(maxSigma)));
}

GLSLGaussianBlur1DFragmentProcessor::GLSLGaussianBlur1DFragmentProcessor(
    PlacementPtr<FragmentProcessor> processor, float sigma, GaussianBlurDirection direction,
    float stepLength, int maxSigma)
    : GaussianBlur1DFragmentProcessor(std::move(processor), sigma, direction, stepLength,
                                      maxSigma) {
}

void GLSLGaussianBlur1DFragmentProcessor::emitCode(EmitArgs& args) const {
  auto fragBuilder = args.fragBuilder;

  std::string sigmaName =
      args.uniformHandler->addUniform("Sigma", UniformFormat::Float, ShaderStage::Fragment);
  std::string texelSizeName =
      args.uniformHandler->addUniform("Step", UniformFormat::Float2, ShaderStage::Fragment);

  fragBuilder->codeAppendf("vec2 offset = %s;", texelSizeName.c_str());

  fragBuilder->codeAppendf("float sigma = %s;", sigmaName.c_str());
  fragBuilder->codeAppend("int radius = int(ceil(2.0 * sigma));");
  fragBuilder->codeAppend("vec4 sum = vec4(0.0);");
  fragBuilder->codeAppend("float total = 0.0;");

  fragBuilder->codeAppendf("for (int j = 0; j <= %d; ++j) {", 4 * maxSigma);
  fragBuilder->codeAppend("int i = j - radius;");
  fragBuilder->codeAppend("float weight = exp(-float(i*i) / (2.0*sigma*sigma));");
  fragBuilder->codeAppend("total += weight;");

  std::string tempColor = "tempColor";
  emitChild(0, &tempColor, args, [](std::string_view coord) {
    return "(" + std::string(coord) + " + offset * float(i))";
  });

  fragBuilder->codeAppendf("sum += %s * weight;", tempColor.c_str());
  fragBuilder->codeAppend("if (i == radius) { break; }");
  fragBuilder->codeAppend("}");
  fragBuilder->codeAppendf("%s = sum / total;", args.outputColor.c_str());
}

void GLSLGaussianBlur1DFragmentProcessor::onSetData(UniformData* /*vertexUniformData*/,
                                                    UniformData* fragmentUniformData) const {
  auto processor = childProcessor(0);
  Point stepVectors[] = {{0, 0}, {stepLength, 0}};
  if (direction == GaussianBlurDirection::Vertical) {
    stepVectors[1] = {0, stepLength};
  }

  DEBUG_ASSERT(processor->numCoordTransforms() == 1);
  auto transform = processor->coordTransform(0);
  auto matrix = transform->getTotalMatrix();
  matrix.mapPoints(stepVectors, 2);
  Point step = stepVectors[1] - stepVectors[0];
  fragmentUniformData->setData("Sigma", sigma);
  fragmentUniformData->setData("Step", step);
}
}  // namespace tgfx

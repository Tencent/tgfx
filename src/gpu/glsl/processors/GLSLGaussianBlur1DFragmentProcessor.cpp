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
    BlockAllocator* allocator, PlacementPtr<FragmentProcessor> processor, float sigma,
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

  return allocator->make<GLSLGaussianBlur1DFragmentProcessor>(
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
  fragBuilder->codeAppend("float twoSigmaSq = 2.0 * sigma * sigma;");
  fragBuilder->codeAppend("int halfRadius = (radius + 1) / 2;");
  fragBuilder->codeAppend("vec4 sum = vec4(0.0);");
  fragBuilder->codeAppend("float total = 0.0;");
  fragBuilder->codeAppend("float currentOffset = 0.0;");
  fragBuilder->codeAppend("float currentWeight = 0.0;");

  // Merge each adjacent texel pair into a single bilinear fetch to halve the
  // sample count. See https://www.rastergrid.com/blog/2010/09/efficient-gaussian-blur-with-linear-sampling/

  fragBuilder->codeAppendf("for (int j = 0; j <= %d; ++j) {", 2 * maxSigma);
  fragBuilder->codeAppendf("int pairIdx = j - %d;", maxSigma);
  fragBuilder->codeAppend("int absPair = abs(pairIdx);");
  fragBuilder->codeAppend("if (absPair > halfRadius) { continue; }");
  fragBuilder->codeAppend("if (pairIdx == 0) {");
  fragBuilder->codeAppend("  currentOffset = 0.0;");
  fragBuilder->codeAppend("  currentWeight = 1.0;");
  fragBuilder->codeAppend("} else {");
  fragBuilder->codeAppend("  int i1 = 2 * absPair - 1;");
  fragBuilder->codeAppend("  int i2 = min(2 * absPair, radius);");
  fragBuilder->codeAppend("  float o1 = float(i1);");
  fragBuilder->codeAppend("  float o2 = float(i2);");
  fragBuilder->codeAppend("  float w1 = exp(-(o1 * o1) / twoSigmaSq);");
  fragBuilder->codeAppend("  float w2 = (i2 != i1) ? exp(-(o2 * o2) / twoSigmaSq) : 0.0;");
  fragBuilder->codeAppend("  currentWeight = w1 + w2;");
  fragBuilder->codeAppend("  float t = (o1 * w1 + o2 * w2) / currentWeight;");
  fragBuilder->codeAppend("  currentOffset = (pairIdx > 0) ? t : -t;");
  fragBuilder->codeAppend("}");
  fragBuilder->codeAppend("total += currentWeight;");

  std::string tempColor = "tempColor";
  emitChild(0, &tempColor, args, [](std::string_view coord) {
    return "(" + std::string(coord) + " + offset * currentOffset)";
  });

  fragBuilder->codeAppendf("sum += %s * currentWeight;", tempColor.c_str());
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

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
#include <cmath>
#include <string>
#include <string_view>

namespace tgfx {

// Returns the shader expression for sampling the child at a coord offset by the gaussian kernel.
// "offset" is a shader variable declared in the emitted code and "i" is the loop index.
static std::string GaussianOffsetCoordFunc(std::string_view coord) {
  return "(" + std::string(coord) + " + offset * float(i))";
}

PlacementPtr<FragmentProcessor> GaussianBlur1DFragmentProcessor::Make(
    BlockAllocator* allocator, PlacementPtr<FragmentProcessor> processor, float sigma,
    GaussianBlurDirection direction, float stepLength, int maxSigma) {
  if (!processor) {
    return nullptr;
  }
  if (maxSigma < 0) {
    return nullptr;
  }
  if (sigma <= 0 || stepLength <= 0 || !std::isfinite(sigma)) {
    return processor;
  }
  // The kernel table and the shader loop bound are dimensioned for maxSigma and sigma, so the
  // caller must respect both limits. Clamping here would silently mask contract violations.
  DEBUG_ASSERT(maxSigma <= MAX_KERNEL_RADIUS / 2);
  DEBUG_ASSERT(sigma <= static_cast<float>(maxSigma));

  return allocator->make<GLSLGaussianBlur1DFragmentProcessor>(std::move(processor), sigma,
                                                              direction, stepLength, maxSigma);
}

GLSLGaussianBlur1DFragmentProcessor::GLSLGaussianBlur1DFragmentProcessor(
    PlacementPtr<FragmentProcessor> processor, float sigma, GaussianBlurDirection direction,
    float stepLength, int maxSigma)
    : GaussianBlur1DFragmentProcessor(std::move(processor), sigma, direction, stepLength,
                                      maxSigma) {
}

void GLSLGaussianBlur1DFragmentProcessor::emitCode(EmitArgs& args) const {
  auto fragBuilder = args.fragBuilder;

  // The normalized half-kernel weights are precomputed on the CPU and uploaded as a vec4 array.
  std::string kernelName = args.uniformHandler->addUniform(
      "Kernel", UniformFormat::Float4, ShaderStage::Fragment, KERNEL_VEC4_COUNT);
  std::string radiusName =
      args.uniformHandler->addUniform("Radius", UniformFormat::Int, ShaderStage::Fragment);
  std::string texelSizeName =
      args.uniformHandler->addUniform("Step", UniformFormat::Float2, ShaderStage::Fragment);

  fragBuilder->codeAppendf("vec2 offset = %s;", texelSizeName.c_str());
  fragBuilder->codeAppendf("int radius = %s;", radiusName.c_str());
  fragBuilder->codeAppend("vec4 sum = vec4(0.0);");

  // The kernel is symmetric, so abs(i) indexes the half-kernel table. The weights are already
  // normalized on the CPU, so neither exp() nor the trailing normalization division is needed.
  fragBuilder->codeAppendf("for (int j = 0; j <= %d; ++j) {", kernelLoopUpperBound());
  fragBuilder->codeAppend("int i = j - radius;");
  fragBuilder->codeAppend("if (i > radius) { break; }");
  fragBuilder->codeAppendf("float weight = %s[abs(i) / 4][abs(i) %% 4];", kernelName.c_str());

  std::string tempColor = "tempColor";
  emitChild(0, &tempColor, args, GaussianOffsetCoordFunc);

  fragBuilder->codeAppendf("sum += %s * weight;", tempColor.c_str());
  fragBuilder->codeAppend("}");
  fragBuilder->codeAppendf("%s = sum;", args.outputColor.c_str());
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

  Blur1DFragmentProcessor::setKernelData(fragmentUniformData);
  fragmentUniformData->setData("Step", step);
}
}  // namespace tgfx

/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "GLSLTentBlur1DFragmentProcessor.h"

namespace tgfx {

PlacementPtr<FragmentProcessor> TentBlur1DFragmentProcessor::Make(
    BlockAllocator* allocator, PlacementPtr<FragmentProcessor> processor, float radius,
    TentBlurDirection direction, float stepLength, int maxRadius) {
  if (!processor) {
    return nullptr;
  }
  if (maxRadius < 0) {
    return nullptr;
  }
  if (radius <= 0 || stepLength <= 0) {
    return processor;
  }

  return allocator->make<GLSLTentBlur1DFragmentProcessor>(
      std::move(processor), radius, direction, stepLength, static_cast<int>(ceil(maxRadius)));
}

GLSLTentBlur1DFragmentProcessor::GLSLTentBlur1DFragmentProcessor(
    PlacementPtr<FragmentProcessor> processor, float radius, TentBlurDirection direction,
    float stepLength, int maxRadius)
    : TentBlur1DFragmentProcessor(std::move(processor), radius, direction, stepLength, maxRadius) {
}

void GLSLTentBlur1DFragmentProcessor::emitCode(EmitArgs& args) const {
  auto fragBuilder = args.fragBuilder;

  std::string radiusName =
      args.uniformHandler->addUniform("Radius", UniformFormat::Float, ShaderStage::Fragment);
  std::string texelSizeName =
      args.uniformHandler->addUniform("Step", UniformFormat::Float2, ShaderStage::Fragment);

  fragBuilder->codeAppendf("vec2 offset = %s;", texelSizeName.c_str());

  fragBuilder->codeAppendf("float radius = %s;", radiusName.c_str());
  fragBuilder->codeAppend("int iRadius = int(ceil(radius));");
  fragBuilder->codeAppend("vec4 sum = vec4(0.0);");
  fragBuilder->codeAppend("float total = 0.0;");

  fragBuilder->codeAppendf("for (int j = 0; j <= %d; ++j) {", 2 * maxRadius);
  fragBuilder->codeAppend("int i = j - iRadius;");
  fragBuilder->codeAppend("float weight = max(0.0, radius - abs(float(i)));");
  fragBuilder->codeAppend("total += weight;");

  std::string tempColor = "tempColor";
  emitChild(0, &tempColor, args, [](std::string_view coord) {
    return "(" + std::string(coord) + " + offset * float(i))";
  });

  fragBuilder->codeAppendf("sum += %s * weight;", tempColor.c_str());
  fragBuilder->codeAppend("if (i == iRadius) { break; }");
  fragBuilder->codeAppend("}");
  fragBuilder->codeAppendf("%s = sum / total;", args.outputColor.c_str());
}

void GLSLTentBlur1DFragmentProcessor::onSetData(UniformData* /*vertexUniformData*/,
                                                UniformData* fragmentUniformData) const {
  auto processor = childProcessor(0);
  Point stepVectors[] = {{0, 0}, {stepLength, 0}};
  if (direction == TentBlurDirection::Vertical) {
    stepVectors[1] = {0, stepLength};
  }

  DEBUG_ASSERT(processor->numCoordTransforms() == 1);
  auto transform = processor->coordTransform(0);
  auto matrix = transform->getTotalMatrix();
  matrix.mapPoints(stepVectors, 2);
  Point step = stepVectors[1] - stepVectors[0];
  fragmentUniformData->setData("Radius", radius);
  fragmentUniformData->setData("Step", step);
}
}  // namespace tgfx

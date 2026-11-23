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

#include "GLSLGaussianBlur2DFragmentProcessor.h"
#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include <string_view>
#include "gpu/UniformData.h"
#include "gpu/glsl/GLSLGaussianKernelWeight.h"

namespace tgfx {

// The child coord of the current sample. The names are shader variables declared in emitCode().
static std::string BlurSampleCoordFunc(std::string_view coord) {
  return "(" + std::string(coord) +
         " + blur2DOrigin + blur2DStepX * float(blur2DX) + blur2DStepY * float(blur2DY))";
}

PlacementPtr<FragmentProcessor> GaussianBlur2DFragmentProcessor::Make(
    BlockAllocator* allocator, PlacementPtr<FragmentProcessor> processor, float sigmaX,
    float sigmaY, const Point& origin) {
  if (!processor || !std::isfinite(sigmaX) || !std::isfinite(sigmaY) || sigmaX < 0.0f ||
      sigmaY < 0.0f || (sigmaX <= 0.0f && sigmaY <= 0.0f) || sigmaX > MAX_SIGMA ||
      sigmaY > MAX_SIGMA) {
    return nullptr;
  }
  return allocator->make<GLSLGaussianBlur2DFragmentProcessor>(std::move(processor), sigmaX, sigmaY,
                                                              origin);
}

GLSLGaussianBlur2DFragmentProcessor::GLSLGaussianBlur2DFragmentProcessor(
    PlacementPtr<FragmentProcessor> processor, float sigmaX, float sigmaY, const Point& origin)
    : GaussianBlur2DFragmentProcessor(std::move(processor), sigmaX, sigmaY, origin) {
}

void GLSLGaussianBlur2DFragmentProcessor::emitCode(EmitArgs& args) const {
  auto fragBuilder = args.fragBuilder;
  auto uniforms = args.uniformHandler;
  auto kernelXName = uniforms->addUniform("KernelX", UniformFormat::Float4, ShaderStage::Fragment,
                                          KERNEL_VEC4_COUNT);
  auto kernelYName = uniforms->addUniform("KernelY", UniformFormat::Float4, ShaderStage::Fragment,
                                          KERNEL_VEC4_COUNT);
  auto radiusXName = uniforms->addUniform("RadiusX", UniformFormat::Int, ShaderStage::Fragment);
  auto radiusYName = uniforms->addUniform("RadiusY", UniformFormat::Int, ShaderStage::Fragment);
  auto stepXName = uniforms->addUniform("StepX", UniformFormat::Float2, ShaderStage::Fragment);
  auto stepYName = uniforms->addUniform("StepY", UniformFormat::Float2, ShaderStage::Fragment);
  auto originName = uniforms->addUniform("Origin", UniformFormat::Float2, ShaderStage::Fragment);

  fragBuilder->codeAppendf("vec2 blur2DStepX = %s;", stepXName.c_str());
  fragBuilder->codeAppendf("vec2 blur2DStepY = %s;", stepYName.c_str());
  fragBuilder->codeAppendf("vec2 blur2DOrigin = %s;", originName.c_str());
  fragBuilder->codeAppendf("int blur2DRadiusX = %s;", radiusXName.c_str());
  fragBuilder->codeAppendf("int blur2DRadiusY = %s;", radiusYName.c_str());
  fragBuilder->codeAppend("vec4 blur2DSum = vec4(0.0);");
  // Constant loop bounds keep the shader valid on GLSL ES; the radii stop the loops early.
  fragBuilder->codeAppendf("for (int blur2DJY = 0; blur2DJY <= %d; ++blur2DJY) {", 2 * MAX_RADIUS);
  fragBuilder->codeAppend("int blur2DY = blur2DJY - blur2DRadiusY;");
  fragBuilder->codeAppend("if (blur2DY > blur2DRadiusY) { break; }");
  AppendKernelWeight(fragBuilder, kernelYName, "blur2DY", "blur2DWeightY", KERNEL_VEC4_COUNT);
  fragBuilder->codeAppendf("for (int blur2DJX = 0; blur2DJX <= %d; ++blur2DJX) {", 2 * MAX_RADIUS);
  fragBuilder->codeAppend("int blur2DX = blur2DJX - blur2DRadiusX;");
  fragBuilder->codeAppend("if (blur2DX > blur2DRadiusX) { break; }");
  AppendKernelWeight(fragBuilder, kernelXName, "blur2DX", "blur2DWeightX", KERNEL_VEC4_COUNT);
  std::string sampleColor = "blur2DSample";
  emitChild(0, &sampleColor, args, BlurSampleCoordFunc);
  fragBuilder->codeAppendf("blur2DSum += %s * (blur2DWeightX * blur2DWeightY);",
                           sampleColor.c_str());
  fragBuilder->codeAppend("}");
  fragBuilder->codeAppend("}");
  fragBuilder->codeAppendf("%s = blur2DSum;", args.outputColor.c_str());
}

void GLSLGaussianBlur2DFragmentProcessor::onSetData(UniformData* /*vertexUniformData*/,
                                                    UniformData* fragmentUniformData) const {
  auto processor = childProcessor(0);
  DEBUG_ASSERT(processor->numCoordTransforms() == 1);
  // Steps and origin are given in target pixels; the child samples in its own coordinate space.
  auto matrix = processor->coordTransform(0)->getTotalMatrix();
  Point points[] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, origin};
  matrix.mapPoints(points, 4);
  fragmentUniformData->setData("StepX", points[1] - points[0]);
  fragmentUniformData->setData("StepY", points[2] - points[0]);
  fragmentUniformData->setData("Origin", points[3] - points[0]);
  std::array<float, 4 * KERNEL_VEC4_COUNT> kernelData = {};
  memcpy(kernelData.data(), kernelX.data(), static_cast<size_t>(radiusX + 1) * sizeof(float));
  fragmentUniformData->setData("KernelX", kernelData);
  kernelData = {};
  memcpy(kernelData.data(), kernelY.data(), static_cast<size_t>(radiusY + 1) * sizeof(float));
  fragmentUniformData->setData("KernelY", kernelData);
  fragmentUniformData->setData("RadiusX", radiusX);
  fragmentUniformData->setData("RadiusY", radiusY);
}
}  // namespace tgfx

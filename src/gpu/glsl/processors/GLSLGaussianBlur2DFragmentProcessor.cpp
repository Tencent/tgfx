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

#include "gpu/ir/ProgramIREmit.h"
#include "GLSLGaussianBlur2DFragmentProcessor.h"
#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include <string_view>
#include "gpu/UniformData.h"
#include "gpu/glsl/GLSLGaussianKernelWeight.h"

namespace tgfx {

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

  // The IR route: two nested runtime loops over one child emission, the same shape the text
  // loops had (the child fires once; the loops update its tap coordinate at runtime). The
  // kernel slots are this processor's own uniforms, so the weight calls take them as arguments.
  if (args.irBuilder != nullptr) {
    auto ir = args.irBuilder;
    auto intern2 = [&](const std::string& name) {
      return ir->InternValue(name, SLType::Float2, Precision::High, IRValueKind::Uniform);
    };
    const auto radiusXValue = ir->InternValue(radiusXName, SLType::Int, Precision::High,
                                              IRValueKind::Uniform);
    const auto radiusYValue = ir->InternValue(radiusYName, SLType::Int, Precision::High,
                                              IRValueKind::Uniform);
    const auto stepXValue = intern2(stepXName);
    const auto stepYValue = intern2(stepYName);
    const auto originValue = intern2(originName);
    auto slot = [&](const std::string& base, int index) {
      return ir->InternValue(base + "[" + std::to_string(index) + "]", SLType::Float4,
                             Precision::High, IRValueKind::Uniform);
    };
    const auto sumValue = emitWeightedTapLoop2D(
        ir, radiusXValue, radiusYValue, stepXValue, stepYValue, originValue, slot(kernelXName, 0),
        slot(kernelXName, 1), slot(kernelYName, 0), slot(kernelYName, 1),
        [&](ValueID /*tapCoordValue*/, const std::string& tapCoordName) -> ValueID {
          std::string childName = "blur2DTapChild";
          return emitChildIR(
              0, "", &childName, args, INVALID_VALUE_ID,
              [&tapCoordName](std::string_view) -> std::string { return tapCoordName; });
        });
    if (sumValue != INVALID_VALUE_ID && args.outputValue != INVALID_VALUE_ID) {
      ir->StoreOutput(args.outputValue, sumValue);
      return;
    }
    ir->MarkFailed("the gaussian blur 2D path cannot express this child");
    return;
  }
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

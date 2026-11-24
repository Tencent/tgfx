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
#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>

namespace tgfx {

// Returns the shader expression for sampling the child at a coord offset by the gaussian kernel.
// "offset" is a shader variable declared in the emitted code and "i" is the loop index.
static std::string GaussianOffsetCoordFunc(std::string_view coord) {
  return "(" + std::string(coord) + " + offset * float(i))";
}

// Emits the code that reads the half-kernel weight for the current loop offset into a "weight"
// variable. The slot and the lane inside it are picked with chains of compile-time indices rather
// than indexing the uniform array with abs(i) directly: SwiftShader miscompiles a dynamic index
// into a std140 uniform block array and silently reads zeros, which drops the blur.
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

  // The tap loop expresses itself as one call when the child's whole composition is a single
  // shader-tiled texture read: gaussianBlur1D reproduces exactly that read through the same shared
  // mapping functions, so nothing of the child's semantics is dropped. Every other child keeps the
  // inline loop below.
  auto entry = fragBuilder->appendFunction("gaussianBlur1D");
  if (!entry.empty() && args.textureSamplers != nullptr && args.transformedCoords != nullptr) {
    const auto* child = childProcessor(0);
    auto childSamplers = args.textureSamplers->childInputs(0);
    auto childCoords = args.transformedCoords->childInputs(0);
    if (child != nullptr && child->numChildProcessors() == 0 && childSamplers.count() == 1 &&
        childCoords.count() >= 1) {
      fragBuilder->onBeforeChildProcEmitCode(child);
      FragmentProcessor::TiledReadDescription tiled = {};
      const bool described = child->describeTiledRead(args, childSamplers[0], &tiled);
      fragBuilder->onAfterChildProcEmitCode();
      const bool tiledRead = described && !tiled.plainRead;
      if (described && (!tiledRead || (!tiled.subsetName.empty() && !tiled.clampName.empty()))) {
        const auto samplerName = args.uniformHandler->getSamplerVariable(childSamplers[0]).name();
        const std::string subset = tiledRead ? tiled.subsetName : std::string("vec4(0.0)");
        const std::string clamp = tiledRead ? tiled.clampName : std::string("vec4(0.0)");
        const std::string dimension = tiledRead && !tiled.dimensionName.empty()
                                          ? tiled.dimensionName
                                          : std::string("vec2(1.0)");
        std::vector<ValueRef> callArgs = {samplerName,
                                          childCoords[0].name(),
                                          texelSizeName,
                                          radiusName,
                                          subset,
                                          "0",
                                          tiledRead ? "1" : "0",
                                          std::to_string(tiledRead ? tiled.shaderModeX : 0),
                                          std::to_string(tiledRead ? tiled.shaderModeY : 0),
                                          subset,
                                          clamp,
                                          dimension,
                                          tiledRead && tiled.seamBlend ? "1" : "0",
                                          tiledRead && tiled.borderPass ? "1" : "0"};
        for (int slot = 0; slot < 17; ++slot) {
          callArgs.push_back(kernelName + "[" + std::to_string(slot) + "]");
        }
        fragBuilder->appendCall(args.outputColor, entry, callArgs);
        if (args.irBuilder != nullptr && args.outputValue != INVALID_VALUE_ID &&
            childCoords[0].type() == SLType::Float2) {
          const auto samplerValue = args.samplerValue(childSamplers[0]);
          if (samplerValue != INVALID_VALUE_ID) {
            const auto zero = args.irBuilder->CreateConstant(0.0f);
            const auto one = args.irBuilder->CreateConstant(1.0f);
            const auto zero4 =
                args.irBuilder->CreateConstruct(SLType::Float4, {zero, zero, zero, zero});
            const auto one2 = args.irBuilder->CreateConstruct(SLType::Float2, {one, one});
            const auto coordValue = args.irBuilder->InternValue(
                childCoords[0].name(), SLType::Float2, Precision::High, IRValueKind::Local);
            const auto texelSizeValue = args.irBuilder->InternValue(
                texelSizeName, SLType::Float2, Precision::High, IRValueKind::Uniform);
            const auto radiusValue = args.irBuilder->InternValue(
                radiusName, SLType::Int, Precision::High, IRValueKind::Uniform);
            const auto subsetValue =
                tiledRead && tiled.subset != INVALID_VALUE_ID ? tiled.subset : zero4;
            const auto clampValue =
                tiledRead && tiled.clamp != INVALID_VALUE_ID ? tiled.clamp : zero4;
            const auto dimensionValue =
                tiledRead && tiled.dimensions != INVALID_VALUE_ID ? tiled.dimensions : one2;
            const auto kernelValue = args.irBuilder->InternValue(
                kernelName, SLType::Float4, Precision::High, IRValueKind::UniformArray);
            const auto modeXValue =
                args.irBuilder->CreateIntConstant(tiledRead ? tiled.shaderModeX : 0);
            const auto modeYValue =
                args.irBuilder->CreateIntConstant(tiledRead ? tiled.shaderModeY : 0);
            const auto seamFlags =
                TiledFlags(TiledReadFlag::RepeatX) | TiledFlags(TiledReadFlag::RepeatY);
            const auto borderFlags = TiledFlags(TiledReadFlag::BorderLinearX) |
                                     TiledFlags(TiledReadFlag::BorderLinearY) |
                                     TiledFlags(TiledReadFlag::BorderNearestX) |
                                     TiledFlags(TiledReadFlag::BorderNearestY);
            const auto seamValue =
                args.irBuilder->CreateIntConstant((tiled.flags & seamFlags) != 0 ? 1 : 0);
            const auto borderValue =
                args.irBuilder->CreateIntConstant((tiled.flags & borderFlags) != 0 ? 1 : 0);
            const auto result = args.irBuilder->CreateCall(
                OperatorID::GaussianBlur1D, SLType::Float4,
                {samplerValue, coordValue, texelSizeValue, radiusValue, subsetValue, modeXValue,
                 modeYValue, clampValue, dimensionValue, seamValue, borderValue, kernelValue},
                args.outputColor);
            args.irBuilder->StoreOutput(args.outputValue, result);
          }
        }
        return;
      }
    }
  }

  fragBuilder->codeAppendf("vec2 offset = %s;", texelSizeName.c_str());
  fragBuilder->codeAppendf("int radius = %s;", radiusName.c_str());
  fragBuilder->codeAppend("vec4 sum = vec4(0.0);");

  // The kernel is symmetric, so abs(i) indexes the half-kernel table. The weights are already
  // normalized on the CPU, so neither exp() nor the trailing normalization division is needed.
  fragBuilder->codeAppendf("for (int j = 0; j <= %d; ++j) {", kernelLoopUpperBound());
  fragBuilder->codeAppend("int i = j - radius;");
  fragBuilder->codeAppend("if (i > radius) { break; }");
  // abs(i) never exceeds kernelRadius, which computeKernel() derives as ceil(2 * sigma) and thus
  // stays within 2 * maxSigma as long as the sigma <= maxSigma contract holds, upheld by the call
  // site as described in computeKernel(). The loop bound above rests on the same contract.
  auto weightEntry = fragBuilder->appendFunction("tgfxBlurWeight");
  if (weightEntry.empty()) {
    return;
  }
  fragBuilder->codeAppendf("float weight = %s(%s, i);", weightEntry.c_str(), kernelName.c_str());

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

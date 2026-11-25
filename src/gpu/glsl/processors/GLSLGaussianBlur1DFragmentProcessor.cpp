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
#include "GeneratedOperators.h"
#include "gpu/glsl/GLSLOperatorEmitter.h"

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

  // The normalized half-kernel weights are precomputed on the CPU and uploaded as generated
  // uniform slots. The set is declared for every program form (the linked call and the inline
  // loop both read it through tgfxKernelWeight), so EnsureUniforms runs before the path split;
  // it is idempotent, so the Emit call below finds the registration already done.
  GaussianBlur1DOp::UniformNames names = {};
  if (args.irBuilder != nullptr) {
    GaussianBlur1DOp::EnsureUniforms(args.irBuilder, args.uniformHandler, &names);
  }

  // The tap loop expresses itself as one call when the child's whole composition is a single
  // shader-tiled texture read: gaussianBlur1D reproduces exactly that read through the same shared
  // mapping functions, so nothing of the child's semantics is dropped. Every other child keeps the
  // inline loop below. The function text is pulled only on the call path: its body reads the
  // generated uniforms, which only that path declares, so a pulled-but-unused body would not
  // compile.
  if (args.textureSamplers != nullptr && args.transformedCoords != nullptr) {
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
        auto entry = fragBuilder->appendFunction("gaussianBlur1D");
        if (entry.empty()) {
          return;
        }
        const auto samplerName = args.uniformHandler->getSamplerVariable(childSamplers[0]).name();
        const std::string subset = tiledRead ? tiled.subsetName : std::string("vec4(0.0)");
        const std::string clamp = tiledRead ? tiled.clampName : std::string("vec4(0.0)");
        const std::string dimension = tiledRead && !tiled.dimensionName.empty()
                                          ? tiled.dimensionName
                                          : std::string("vec2(1.0)");
        if (args.irBuilder == nullptr) {
          return;
        }
        if (args.outputValue == INVALID_VALUE_ID || childCoords[0].type() != SLType::Float2) {
          args.irBuilder->MarkFailed("the gaussian blur coordinate is not a vec2");
          return;
        }
        const auto samplerValue = args.samplerValue(childSamplers[0]);
        if (samplerValue == INVALID_VALUE_ID) {
          args.irBuilder->MarkFailed("the gaussian blur sampler has no IR value");
          return;
        }
        const auto zero = args.irBuilder->CreateConstant(0.0f);
        const auto one = args.irBuilder->CreateConstant(1.0f);
        const auto zero4 =
            args.irBuilder->CreateConstruct(SLType::Float4, {zero, zero, zero, zero});
        const auto one2 = args.irBuilder->CreateConstruct(SLType::Float2, {one, one});
        // The child coordinate is the fragment input the coord transform declared, so its IR kind
        // is Varying (identified by the same mangled name the varying handler registered).
        const auto coordValue = args.irBuilder->InternValue(childCoords[0].name(), SLType::Float2,
                                                            Precision::High, IRValueKind::Varying);
        const auto subsetValue =
            tiledRead && tiled.subset != INVALID_VALUE_ID ? tiled.subset : zero4;
        const auto clampValue = tiledRead && tiled.clamp != INVALID_VALUE_ID ? tiled.clamp : zero4;
        const auto dimensionValue =
            tiledRead && tiled.dimensions != INVALID_VALUE_ID ? tiled.dimensions : one2;
        const int32_t modeX = tiledRead ? static_cast<int32_t>(tiled.shaderModeX) : 0;
        const int32_t modeY = tiledRead ? static_cast<int32_t>(tiled.shaderModeY) : 0;
        const int32_t tiledSource = modeX != 0 || modeY != 0 ? 1 : 0;
        // The flags mirror TiledReadDescription's own derivation (GLSLTiledTextureEffect.cpp):
        // seam blend for the repeat modes, border pass for the linear border modes only (nearest
        // reads happen in the shading path, not the border pass).
        const auto seamFlags =
            TiledFlags(TiledReadFlag::RepeatX) | TiledFlags(TiledReadFlag::RepeatY);
        const auto borderFlags =
            TiledFlags(TiledReadFlag::BorderLinearX) | TiledFlags(TiledReadFlag::BorderLinearY);
        const int32_t seamBlend = (tiled.flags & seamFlags) != 0 ? 1 : 0;
        const int32_t borderPass = (tiled.flags & borderFlags) != 0 ? 1 : 0;
        const auto result = GaussianBlur1DOp::Emit(
            args.irBuilder, args.uniformHandler, &names, samplerValue, coordValue, subsetValue,
            0 /*subsetEnabled*/, tiledSource, modeX, modeY, subsetValue, clampValue, dimensionValue,
            seamBlend, borderPass);
        if (result == INVALID_VALUE_ID) {
          args.irBuilder->MarkFailed("the gaussian blur call could not be recorded");
          return;
        }
        args.irBuilder->StoreOutput(args.outputValue, result);
        std::string expression = {};
        std::string error = {};
        if (!GLSLOperatorEmitter::EmitCall(args.irBuilder->IR(), result, &expression, &error)) {
          args.irBuilder->MarkFailed(error);
          return;
        }
        std::vector<ValueRef> callArgs = {samplerName,
                                          childCoords[0].name(),
                                          names.tapStep,
                                          names.tapRadius,
                                          subset,
                                          "0",
                                          std::to_string(tiledSource),
                                          std::to_string(modeX),
                                          std::to_string(modeY),
                                          subset,
                                          clamp,
                                          dimension,
                                          std::to_string(seamBlend),
                                          std::to_string(borderPass)};
        callArgs.push_back(names.kernelSlot0);
        callArgs.push_back(names.kernelSlot1);
        callArgs.push_back(names.kernelSlot2);
        callArgs.push_back(names.kernelSlot3);
        callArgs.push_back(names.kernelSlot4);
        callArgs.push_back(names.kernelSlot5);
        callArgs.push_back(names.kernelSlot6);
        callArgs.push_back(names.kernelSlot7);
        callArgs.push_back(names.kernelSlot8);
        callArgs.push_back(names.kernelSlot9);
        callArgs.push_back(names.kernelSlot10);
        callArgs.push_back(names.kernelSlot11);
        callArgs.push_back(names.kernelSlot12);
        callArgs.push_back(names.kernelSlot13);
        callArgs.push_back(names.kernelSlot14);
        callArgs.push_back(names.kernelSlot15);
        callArgs.push_back(names.kernelSlot16);
        fragBuilder->appendCall(args.outputColor, entry, callArgs, "", expression);
        return;
      }
    }
  }

  fragBuilder->codeAppendf("vec2 offset = %s;", names.tapStep.c_str());
  fragBuilder->codeAppendf("int radius = %s;", names.tapRadius.c_str());
  fragBuilder->codeAppend("vec4 sum = vec4(0.0);");

  // The kernel is symmetric, so abs(i) indexes the half-kernel table. The weights are already
  // normalized on the CPU, so neither exp() nor the trailing normalization division is needed.
  fragBuilder->codeAppendf("for (int j = 0; j <= %d; ++j) {", kernelLoopUpperBound());
  fragBuilder->codeAppend("int i = j - radius;");
  fragBuilder->codeAppend("if (i > radius) { break; }");
  // abs(i) never exceeds kernelRadius, which computeKernel() derives as ceil(2 * sigma) and thus
  // stays within 2 * maxSigma as long as the sigma <= maxSigma contract holds, upheld by the call
  // site as described in computeKernel(). The loop bound above rests on the same contract, so the
  // library lookup only reaches the slots that range can hit.
  auto weightEntry = fragBuilder->appendFunction("tgfxKernelWeight");
  if (weightEntry.empty()) {
    return;
  }
  fragBuilder->codeAppendf(
      "float weight = %s(%s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, i);",
      weightEntry.c_str(), names.kernelSlot0.c_str(), names.kernelSlot1.c_str(),
      names.kernelSlot2.c_str(), names.kernelSlot3.c_str(), names.kernelSlot4.c_str(),
      names.kernelSlot5.c_str(), names.kernelSlot6.c_str(), names.kernelSlot7.c_str(),
      names.kernelSlot8.c_str(), names.kernelSlot9.c_str(), names.kernelSlot10.c_str(),
      names.kernelSlot11.c_str(), names.kernelSlot12.c_str(), names.kernelSlot13.c_str(),
      names.kernelSlot14.c_str(), names.kernelSlot15.c_str(), names.kernelSlot16.c_str());

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

  // One uniform set serves both program forms: the kernel slots the shared weight function reads,
  // plus the step and radius.
  GaussianBlur1DUniforms uniforms = {};
  const auto packed = packedKernel();
  uniforms.kernelSlot0 = {packed[0], packed[1], packed[2], packed[3]};
  uniforms.kernelSlot1 = {packed[4], packed[5], packed[6], packed[7]};
  uniforms.kernelSlot2 = {packed[8], packed[9], packed[10], packed[11]};
  uniforms.kernelSlot3 = {packed[12], packed[13], packed[14], packed[15]};
  uniforms.kernelSlot4 = {packed[16], packed[17], packed[18], packed[19]};
  uniforms.kernelSlot5 = {packed[20], packed[21], packed[22], packed[23]};
  uniforms.kernelSlot6 = {packed[24], packed[25], packed[26], packed[27]};
  uniforms.kernelSlot7 = {packed[28], packed[29], packed[30], packed[31]};
  uniforms.kernelSlot8 = {packed[32], packed[33], packed[34], packed[35]};
  uniforms.kernelSlot9 = {packed[36], packed[37], packed[38], packed[39]};
  uniforms.kernelSlot10 = {packed[40], packed[41], packed[42], packed[43]};
  uniforms.kernelSlot11 = {packed[44], packed[45], packed[46], packed[47]};
  uniforms.kernelSlot12 = {packed[48], packed[49], packed[50], packed[51]};
  uniforms.kernelSlot13 = {packed[52], packed[53], packed[54], packed[55]};
  uniforms.kernelSlot14 = {packed[56], packed[57], packed[58], packed[59]};
  uniforms.kernelSlot15 = {packed[60], packed[61], packed[62], packed[63]};
  uniforms.kernelSlot16 = {packed[64], packed[65], packed[66], packed[67]};
  uniforms.tapStep = {step.x, step.y};
  uniforms.tapRadius = getKernelRadius();
  uniforms.upload(fragmentUniformData);
}
}  // namespace tgfx

/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions and
//  limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/shaders/kernels/PerlinNoiseFillShader.h"
#include <optional>
#include "gpu/AOTChainBuilder.h"
#include "gpu/DrawingManager.h"
#include "gpu/ProgramInfo.h"
#include "gpu/ShaderMatchHelpers.h"
#include "gpu/processors/AlphaThresholdFragmentProcessor.h"
#include "gpu/processors/ColorMatrixFragmentProcessor.h"
#include "gpu/processors/ComposeFragmentProcessor.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/processors/LumaFragmentProcessor.h"
#include "gpu/processors/PerlinNoiseFragmentProcessor.h"
#include "gpu/processors/PorterDuffXferProcessor.h"

namespace tgfx {

// Cheap containment pre-gate: the perlin kernel can only serve a tree that actually contains a
// noise source, and the check keeps every other tree shape out of the planning oracle below.
static bool ContainsPerlinSource(const FragmentProcessor* processor) {
  if (processor == nullptr) {
    return false;
  }
  if (IsProcessor<PerlinNoiseFragmentProcessor>(processor)) {
    return true;
  }
  for (size_t index = 0; index < processor->numChildProcessors(); ++index) {
    if (ContainsPerlinSource(processor->childProcessor(index))) {
      return true;
    }
  }
  return false;
}

std::optional<PerlinNoiseFillShader::Inputs> PerlinNoiseFillShader::Extract(
    const ProgramInfo* programInfo, PermutationMatchFailure*) {
  if (RejectsAlphaOnlyTarget(ServesAlphaOnly, programInfo)) {
    return std::nullopt;
  }
  if (!IsPositionOnlyRectGP(programInfo->getGeometryProcessor())) {
    return std::nullopt;
  }
  // The perlin kernel carries no coverage-fold path, so every processor must be a color one.
  if (programInfo->numFragmentProcessors() != programInfo->numColorFragmentProcessors()) {
    return std::nullopt;
  }
  bool hasPerlin = false;
  for (size_t index = 0; !hasPerlin && index < programInfo->numFragmentProcessors(); ++index) {
    hasPerlin = ContainsPerlinSource(programInfo->getFragmentProcessor(index));
  }
  if (!hasPerlin) {
    return std::nullopt;
  }
  // The acceptance oracle: the kernel folds up to three trailing operators (pointwise ops, const
  // colors, blends whose other operand is a const) after the noise into its slot records, which
  // is exactly the single-perlin-pass planning. A tree that does not reduce onto that pass (a
  // chain-shaped blend of computed operands, a fourth operator, a non-perlin root) is rejected
  // here and stays with the families behind this one.
  auto context = programInfo->getContext();
  if (context == nullptr || context->drawingManager() == nullptr) {
    return std::nullopt;
  }
  auto expression = AOTChainBuilder::BuildPerlinProcessorForInfo(
      context->drawingManager()->drawingAllocator(), programInfo);
  if (expression == nullptr) {
    return std::nullopt;
  }
  // Keep the built expression on the info (see PointwiseChainShader::Extract): the winner's
  // writer reuses it instead of rebuilding it per draw.
  programInfo->setCachedReexpression(std::move(expression));
  int xpType = GetXPType(programInfo);
  if (xpType < XP_NONE) {
    return std::nullopt;
  }
  return Inputs{xpType};
}

}  // namespace tgfx

/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//       https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions and
//  limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/shaders/level1/PerlinNoiseFillShader.h"
#include <optional>
#include <vector>
#include "gpu/PermutationMatcher.h"
#include "gpu/ProgramInfo.h"
#include "gpu/ShaderMatchHelpers.h"
#include "gpu/processors/FragmentProcessor.h"

namespace tgfx {

std::optional<PermutationMatchResult> PerlinNoiseFillShader::Match(const ProgramInfo* programInfo) {
  if (!IsPositionOnlyRectGP(programInfo->getGeometryProcessor())) {
    return std::nullopt;
  }
  // Accept a PerlinNoiseFragmentProcessor optionally followed by one pointwise operator, in the
  // same two structurally-equivalent forms as TexturedEffectShader::Match:
  //   (a) a bare PerlinNoiseFragmentProcessor (OpType == OP_NONE, set by the Perlin FP), or
  //   (b) Compose(Perlin, op) / sequential [Perlin, op] where op is a supported pointwise operator.
  // The operator is folded into the shared OpType runtime uniform, so the composed case hits the
  // same PerlinNoiseFillShader variant losslessly (no materialization, no extra compile-time axis).
  const FragmentProcessor* perlin = nullptr;
  if (programInfo->numFragmentProcessors() == 1 && programInfo->numColorFragmentProcessors() == 1) {
    auto fp = programInfo->getFragmentProcessor(0);
    if (fp->name() == "PerlinNoiseFragmentProcessor") {
      perlin = fp;
    } else if (fp->name() == "ComposeFragmentProcessor" && fp->numChildProcessors() == 2 &&
               fp->childProcessor(0)->name() == "PerlinNoiseFragmentProcessor" &&
               IsSupportedPointwiseOp(fp->childProcessor(1))) {
      perlin = fp->childProcessor(0);
    }
  } else if (programInfo->numFragmentProcessors() == 2 &&
             programInfo->numColorFragmentProcessors() == 2 &&
             programInfo->getFragmentProcessor(0)->name() == "PerlinNoiseFragmentProcessor" &&
             IsSupportedPointwiseOp(programInfo->getFragmentProcessor(1))) {
    perlin = programInfo->getFragmentProcessor(0);
  }
  if (perlin == nullptr) {
    return std::nullopt;
  }
  int xpType = GetXPType(programInfo);
  if (xpType < 0) {
    return std::nullopt;
  }
  auto fragIndex = D::domain().encode(std::vector<int>{xpType});
  return PermutationMatchResult{"PerlinNoiseFillShader", 0, fragIndex};
}

}  // namespace tgfx

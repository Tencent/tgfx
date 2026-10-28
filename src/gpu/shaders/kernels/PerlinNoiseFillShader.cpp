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
#include "gpu/ProgramInfo.h"
#include "gpu/ShaderMatchHelpers.h"
#include "gpu/processors/ComposeFragmentProcessor.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/processors/PerlinNoiseFragmentProcessor.h"
#include "gpu/processors/PorterDuffXferProcessor.h"
#include "gpu/shaders/modules/ParamWriter.h"
#include "gpu/shaders/modules/PorterDuffXPModule.h"

namespace tgfx {

std::optional<PerlinNoiseFillShader::Inputs> PerlinNoiseFillShader::Extract(
    const ProgramInfo* programInfo) {
  if (RejectsAlphaOnlyTarget(ServesAlphaOnly, programInfo)) {
    return std::nullopt;
  }
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
    if (IsProcessor<PerlinNoiseFragmentProcessor>(fp)) {
      perlin = fp;
    } else if (IsProcessor<ComposeFragmentProcessor>(fp) && fp->numChildProcessors() == 2 &&
               IsProcessor<PerlinNoiseFragmentProcessor>(fp->childProcessor(0)) &&
               IsSupportedPointwiseOp(fp->childProcessor(1))) {
      perlin = fp->childProcessor(0);
    }
  } else if (programInfo->numFragmentProcessors() == 2 &&
             programInfo->numColorFragmentProcessors() == 2 &&
             IsProcessor<PerlinNoiseFragmentProcessor>(programInfo->getFragmentProcessor(0)) &&
             IsSupportedPointwiseOp(programInfo->getFragmentProcessor(1))) {
    perlin = programInfo->getFragmentProcessor(0);
  }
  if (perlin == nullptr) {
    return std::nullopt;
  }
  int xpType = GetXPType(programInfo);
  if (xpType < XP_NONE) {
    return std::nullopt;
  }
  return Inputs{xpType};
}

XPBindFn PerlinNoiseFillShader::XPBindingFor(const ProgramInfo* programInfo) {
  auto* xp = programInfo->getXferProcessor();
  if (!IsProcessor<PorterDuffXferProcessor>(xp)) {
    return nullptr;
  }
  return &PorterDuffXPModule::Bind;
}

}  // namespace tgfx

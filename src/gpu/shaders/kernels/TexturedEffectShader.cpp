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

#include "gpu/shaders/kernels/TexturedEffectShader.h"
#include <optional>
#include "gpu/ProgramInfo.h"
#include "gpu/ShaderMatchHelpers.h"
#include "gpu/processors/AlphaThresholdFragmentProcessor.h"
#include "gpu/processors/ColorMatrixFragmentProcessor.h"
#include "gpu/processors/ComposeFragmentProcessor.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/processors/LumaFragmentProcessor.h"
#include "gpu/processors/PorterDuffXferProcessor.h"
#include "gpu/processors/TextureEffect.h"

namespace tgfx {

std::optional<TexturedEffectShader::Inputs> TexturedEffectShader::Extract(
    const ProgramInfo* programInfo) {
  if (RejectsAlphaOnlyTarget(ServesAlphaOnly, programInfo)) {
    return std::nullopt;
  }
  if (!IsPositionOnlyRectGP(programInfo->getGeometryProcessor())) {
    return std::nullopt;
  }
  int xpType = GetXPType(programInfo);
  if (xpType < XP_NONE) {
    return std::nullopt;
  }
  // Accept two structurally-equivalent forms of "texture followed by one pointwise transform":
  //   (a) a single ComposeFragmentProcessor(TextureEffect, X), or
  //   (b) two sequential top-level color processors [TextureEffect, X].
  // Sequential color processors are chained (each consumes the previous output), so [A, B] produces
  // the identical result to Compose(A, B) and maps to the same precompiled variant.
  const FragmentProcessor* child0 = nullptr;
  const FragmentProcessor* child1 = nullptr;
  if (programInfo->numFragmentProcessors() == 1 && programInfo->numColorFragmentProcessors() == 1) {
    auto fp = programInfo->getFragmentProcessor(0);
    if (!IsProcessor<ComposeFragmentProcessor>(fp) || fp->numChildProcessors() != 2) {
      return std::nullopt;
    }
    child0 = fp->childProcessor(0);
    child1 = fp->childProcessor(1);
  } else if (programInfo->numFragmentProcessors() == 2 &&
             programInfo->numColorFragmentProcessors() == 2) {
    child0 = programInfo->getFragmentProcessor(0);
    child1 = programInfo->getFragmentProcessor(1);
  } else {
    return std::nullopt;
  }
  if (!IsProcessor<TextureEffect>(child0)) {
    return std::nullopt;
  }
  auto* te = static_cast<const TextureEffect*>(child0);
  if (te->isYUV() || te->isAlphaOnly() || te->hasRGBAAA()) {
    return std::nullopt;
  }
  // The precompiled vertex template transforms the coordinate affinely; a projective source must
  // stay off this artifact (the plain templates transform coordinates affinely, with no w division).
  if (te->hasPerspective()) {
    return std::nullopt;
  }
  // All four pointwise operators (ColorMatrix / Luma / AlphaThreshold / ColorSpaceXform) share the
  // same structural class (1 sampler, no CoverageFP, RGBA) and thus the same precompiled shader:
  // TexturedEffectShader. The operator is selected at draw time by the OpType runtime uniform (set
  // by the pointwise FP's onSetData), so it is not a compile-time dimension. The frag index only
  // encodes HAS_XP; subset clamping is a runtime uniform.
  if (!IsSupportedPointwiseOp(child1)) {
    return std::nullopt;
  }
  return Inputs{xpType};
}

}  // namespace tgfx

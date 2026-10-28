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

#include "gpu/shaders/kernels/ShapeInstancedTextureCoverageShader.h"
#include <optional>
#include "gpu/PermutationMatcher.h"
#include "gpu/ProgramInfo.h"
#include "gpu/ShaderMatchHelpers.h"
#include "gpu/processors/ClampedGradientEffect.h"
#include "gpu/processors/ConicGradientLayout.h"
#include "gpu/processors/DeviceSpaceTextureEffect.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/processors/RRectEffect.h"
#include "gpu/processors/RectEffect.h"
#include "gpu/processors/ShapeInstancedGeometryProcessor.h"
#include "gpu/processors/SingleIntervalGradientColorizer.h"
#include "gpu/processors/TextureEffect.h"

namespace tgfx {

static bool IsAnalyticClipKind(const std::optional<CoverageKind>& kind) {
  return kind && (*kind == CoverageKind::AARect || *kind == CoverageKind::RRect);
}

std::optional<ShapeInstancedTextureCoverageShader::Inputs>
ShapeInstancedTextureCoverageShader::Extract(const ProgramInfo* programInfo) {
  if (RejectsAlphaOnlyTarget(ServesAlphaOnly, programInfo)) {
    return std::nullopt;
  }
  auto gp = programInfo->getGeometryProcessor();
  if (!IsProcessor<ShapeInstancedGeometryProcessor>(gp) || GetXPType(programInfo) != XP_NONE) {
    return std::nullopt;
  }
  // Two forms: a bare coverage texture, or a single-interval gradient color FP ahead of it.
  int gradient = 0;
  size_t colorCount = programInfo->numColorFragmentProcessors();
  if (colorCount == 1) {
    auto colorFP = programInfo->getFragmentProcessor(0);
    if (!IsProcessor<ClampedGradientEffect>(colorFP) || colorFP->numChildProcessors() != 2 ||
        !IsProcessor<SingleIntervalGradientColorizer>(colorFP->childProcessor(0))) {
      return std::nullopt;
    }
    auto layout = colorFP->childProcessor(1);
    if (layout->numCoordTransforms() != 1 || layout->coordTransform(0)->matrix.hasPerspective()) {
      return std::nullopt;
    }
    gradient = 1;
  } else if (colorCount != 0) {
    return std::nullopt;
  }
  // One coverage texture, optionally preceded/followed by an analytic clip. The clip rides the
  // runtime clip contract uniforms (no permutation change), so both forms select the same variant.
  auto fpCount = programInfo->numFragmentProcessors();
  const FragmentProcessor* fp = nullptr;
  if (fpCount == colorCount + 1) {
    fp = programInfo->getFragmentProcessor(colorCount);
  } else if (fpCount == colorCount + 2) {
    auto first = programInfo->getFragmentProcessor(colorCount);
    auto second = programInfo->getFragmentProcessor(colorCount + 1);
    auto firstKind = ClassifyCoverage(first);
    auto secondKind = ClassifyCoverage(second);
    if (IsAnalyticClipKind(firstKind) && !IsAnalyticClipKind(secondKind)) {
      fp = second;
    } else if (IsAnalyticClipKind(secondKind) && !IsAnalyticClipKind(firstKind)) {
      fp = first;
    } else {
      // Two masks, or two clips, are not a shape this kernel evaluates.
      return Fail(PermutationMatchFailure::CoverageShape);
    }
  } else {
    return std::nullopt;
  }
  if (!IsProcessor<TextureEffect>(fp)) {
    return std::nullopt;
  }
  auto* texture = static_cast<const TextureEffect*>(fp);
  if (texture->numTextureSamplers() != 1 || texture->isYUV() || texture->hasRGBAAA()) {
    return std::nullopt;
  }
  auto* shape = static_cast<const ShapeInstancedGeometryProcessor*>(gp);
  // The color attribute is unconditional (white broadcast for colorless batches), so both forms
  // read the same vColor varying. This kernel carries no AA coverage attribute.
  if (shape->getAAType() != AAType::None) {
    return std::nullopt;
  }
  return Inputs{gradient != 0};
}

}  // namespace tgfx

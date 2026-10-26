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

#include "gpu/shaders/level1/YUVTextureFillShader.h"
#include <optional>
#include <vector>
#include "gpu/PermutationMatcher.h"
#include "gpu/ProgramInfo.h"
#include "gpu/ShaderMatchHelpers.h"
#include "gpu/processors/AOTYUVChainProcessor.h"
#include "gpu/processors/QuadPerEdgeAAGeometryProcessor.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/shaders/PermutationRules.h"

namespace tgfx {

std::optional<PermutationVariant> YUVTextureFillShader::Match(const ProgramInfo* programInfo) {
  if (RejectsAlphaOnlyTarget(ServesAlphaOnly, programInfo)) {
    return std::nullopt;
  }
  auto gp = programInfo->getGeometryProcessor();
  if (gp->name() != GpClassName) {
    return std::nullopt;
  }
  auto* quadGP = static_cast<const QuadPerEdgeAAGeometryProcessor*>(gp);
  // The precompiled vert has no texSubset attribute, so subset-carrying quads fall back.
  if (quadGP->getHasSubset()) {
    return std::nullopt;
  }
  if (programInfo->numColorFragmentProcessors() != 1 || programInfo->numFragmentProcessors() != 1) {
    return std::nullopt;
  }
  auto fp = programInfo->getFragmentProcessor(0);
  const TextureEffect* texture = nullptr;
  if (IsProcessor<AOTYUVChainProcessor>(fp)) {
    // The decomposed color-grade route: the multi-plane source plus folded pointwise slots,
    // all carried by the same YUVTextureFillShader variant set (the operators are per-draw
    // uniform data, so no dimension changes).
    auto* chain = static_cast<const AOTYUVChainProcessor*>(fp);
    if (chain->source() == nullptr || !IsProcessor<TextureEffect>(chain->source())) {
      return std::nullopt;
    }
    texture = static_cast<const TextureEffect*>(chain->source());
  } else if (IsProcessor<TextureEffect>(fp)) {
    texture = static_cast<const TextureEffect*>(fp);
  } else {
    return std::nullopt;
  }
  if (!texture->isYUV()) {
    return std::nullopt;
  }
  // Only I420 and NV12 have precompiled plane layouts.
  YUVTextureFillInputs inputs;
  inputs.hasUVMatrix = quadGP->hasUVMatrix();
  if (texture->yuvFormat() == YUVFormat::I420) {
    inputs.yuvFormat = 0;
  } else if (texture->yuvFormat() == YUVFormat::NV12) {
    inputs.yuvFormat = 1;
  }
  inputs.xpType = GetXPType(programInfo);
  auto composed = ComposeYUVTextureFill(inputs);
  if (!composed) {
    return std::nullopt;
  }
  auto vertIndex = YUVTextureFillShader::VD::domain().encode(composed->vertValues);
  auto fragIndex = YUVTextureFillShader::FD::domain().encode(composed->fragValues);
  return PermutationVariant{vertIndex, fragIndex};
}

}  // namespace tgfx

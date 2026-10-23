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

#include "gpu/shaders/level1/MaskFillShader.h"
#include <optional>
#include <vector>
#include "gpu/PermutationMatcher.h"
#include "gpu/ProgramInfo.h"
#include "gpu/ShaderMatchHelpers.h"
#include "gpu/processors/TextureEffect.h"

namespace tgfx {

std::optional<PermutationMatchResult> MaskFillShader::Match(const ProgramInfo* programInfo) {
  if (RejectsAlphaOnlyTarget(ServesAlphaOnly, programInfo)) {
    return std::nullopt;
  }
  auto gp = programInfo->getGeometryProcessor();
  if (gp->name() != GpClassName) {
    return std::nullopt;
  }
  // Solid color (from the DefaultGP Color uniform) masked by a single alpha-only coverage
  // TextureEffect (a rasterized shape mask from ShapeDrawOp). No color fragment processors.
  if (programInfo->numColorFragmentProcessors() != 0) {
    return std::nullopt;
  }
  if (programInfo->numFragmentProcessors() != 1) {
    return std::nullopt;
  }
  auto fp = programInfo->getFragmentProcessor(0);
  if (fp->name() != "TextureEffect") {
    return std::nullopt;
  }
  auto* te = static_cast<const TextureEffect*>(fp);
  // Only the simple mask form is supported: a plain alpha-only texture with no subset, no packed
  // alpha, no YUV. Anything else falls back to ProgramBuilder.
  if (te->numTextureSamplers() == 0 || te->isYUV() || !te->isAlphaOnly() || te->hasRGBAAA() ||
      te->hasSubset()) {
    return std::nullopt;
  }
  // The mask coverage feeds the XferProcessor's coverage lerp, so Porter-Duff blends (DST_TEX and
  // framebuffer-fetch) are supported via the HAS_XP fragment dimension. Only a genuinely
  // unsupported transfer processor (xpType < 0) falls back to ProgramBuilder.
  auto xpType = GetXPType(programInfo);
  if (xpType < 0) {
    return std::nullopt;
  }
  auto fragIndex = D::domain().encode(std::vector<int>{xpType});
  return PermutationMatchResult{"MaskFillShader", 0, fragIndex};
}

}  // namespace tgfx

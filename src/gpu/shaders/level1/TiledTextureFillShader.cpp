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

#include "gpu/shaders/level1/TiledTextureFillShader.h"
#include <optional>
#include "gpu/ProgramInfo.h"
#include "gpu/ShaderMatchHelpers.h"
#include "gpu/processors/TiledTextureEffect.h"

namespace tgfx {

std::optional<TiledTextureFillShader::Inputs> TiledTextureFillShader::Extract(
    const ProgramInfo* programInfo) {
  if (RejectsAlphaOnlyTarget(ServesAlphaOnly, programInfo)) {
    return std::nullopt;
  }
  if (!IsPositionOnlyRectGP(programInfo->getGeometryProcessor())) {
    return std::nullopt;
  }
  if (programInfo->numFragmentProcessors() != 1) {
    return std::nullopt;
  }
  int xpType = GetXPType(programInfo);
  if (xpType < XP_NONE) {
    return std::nullopt;
  }
  auto fp = programInfo->getFragmentProcessor(0);
  if (!IsProcessor<TiledTextureEffect>(fp)) {
    return std::nullopt;
  }
  auto* tte = static_cast<const TiledTextureEffect*>(fp);
  if (tte->hasPerspective()) {
    return std::nullopt;
  }
  int modeX = 0;
  int modeY = 0;
  tte->getShaderModes(&modeX, &modeY);
  if (!TiledFillModeSupported(modeX) || !TiledFillModeSupported(modeY)) {
    return std::nullopt;
  }
  // A both-None (0/0) mode means the hardware sampler already resolves the tiling (e.g. Repeat
  // with a full-boundary subset), so this kernel serves it like any other mode.
  // ALPHA_ONLY and HAS_STRICT are folded into runtime uniforms (AlphaOnly / Strict), set by
  // GLSLTiledTextureEffect::onSetData; they are no longer compile-time dimensions.
  return Inputs{xpType};
}

}  // namespace tgfx

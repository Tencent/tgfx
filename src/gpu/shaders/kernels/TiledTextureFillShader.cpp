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

#include "gpu/shaders/kernels/TiledTextureFillShader.h"
#include <cstdio>
#include <optional>
#include "gpu/ProgramInfo.h"
#include "gpu/ShaderMatchHelpers.h"
#include "gpu/aot/AOTChainBuilder.h"
#include "gpu/aot/DrawShape.h"
#include "gpu/processors/TiledTextureEffect.h"

namespace tgfx {

std::optional<TiledTextureFillShader::Inputs> TiledTextureFillShader::ExtractFacts(
    const DrawShape& facts, PermutationMatchFailure*) {
  if (!facts.positionOnlyRect()) {
    return std::nullopt;
  }
  if (facts.fragmentCount != 1) {
    return std::nullopt;
  }
  int xpType = facts.xpType();
  if (xpType < XP_NONE) {
    return std::nullopt;
  }
  // The sole color processor is the IR's tiled texture leaf (no operators above it).
  if (facts.ir == nullptr) {
    return std::nullopt;
  }
  AOTChainBuilder::AOTTextureTailShape tail = {};
  if (!AOTChainBuilder::CollectTextureTail(facts.ir->color, &tail) || !tail.ops.empty()) {
    return std::nullopt;
  }
  auto* leaf = tail.leaf;
  if (leaf->samplingKind != AOTTextureSamplingKind::Tiled || leaf->hasPerspective ||
      !leaf->tiledRecipe.has_value()) {
    return std::nullopt;
  }
  if (!TiledFillModeSupported(static_cast<int>(leaf->tiledRecipe->shaderModeX)) ||
      !TiledFillModeSupported(static_cast<int>(leaf->tiledRecipe->shaderModeY))) {
    return std::nullopt;
  }
  // A both-None (0/0) mode means the hardware sampler already resolves the tiling (e.g. Repeat
  // with a full-boundary subset), so this kernel serves it like any other mode.
  // ALPHA_ONLY and HAS_STRICT are folded into runtime uniforms (AlphaOnly / Strict), set by
  // GLSLTiledTextureEffect::onSetData; they are no longer compile-time dimensions.
  return Inputs{xpType};
}

}  // namespace tgfx

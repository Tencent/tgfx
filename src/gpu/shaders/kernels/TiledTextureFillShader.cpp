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
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/shaders/kernels/TiledTextureFillShader.h"
#include "gpu/ProgramInfo.h"
#include "gpu/aot/ir/DrawIR.h"
#include "gpu/aot/chain/AOTChainBuilder.h"
#include "gpu/shaders/contract/DrawFacts.h"
#include "gpu/aot/select/ShaderMatchHelpers.h"

namespace tgfx {

bool TiledTextureFillShader::Accept(const DrawShape& shape) {
  if (!shape.positionOnlyRect()) {
    return false;
  }
  if (shape.fragmentCount != 1) {
    return false;
  }
  if (shape.xpType() < XP_NONE) {
    return false;
  }
  // The sole color processor is the IR's tiled texture leaf (no operators above it).
  if (shape.ir == nullptr) {
    return false;
  }
  AOTChainBuilder::AOTTextureTailShape tail = {};
  if (!AOTChainBuilder::CollectTextureTail(shape.ir->color, &tail) || !tail.ops.empty()) {
    return false;
  }
  auto* leaf = tail.leaf;
  if (leaf->samplingKind != AOTTextureSamplingKind::Tiled || leaf->hasPerspective ||
      !leaf->tiledRecipe.has_value()) {
    return false;
  }
  if (!TiledFillModeSupported(static_cast<int>(leaf->tiledRecipe->shaderModeX)) ||
      !TiledFillModeSupported(static_cast<int>(leaf->tiledRecipe->shaderModeY))) {
    return false;
  }
  // A both-None (0/0) mode means the hardware sampler already resolves the tiling (e.g. Repeat
  // with a full-boundary subset), so this kernel serves it like any other mode.
  return true;
}

Variants TiledTextureFillShader::Select(const DrawShape& shape) {
  Variants variants;
  variants.frag(XP_DEFINE_NAME, shape.xpType());
  return variants;
}

}  // namespace tgfx

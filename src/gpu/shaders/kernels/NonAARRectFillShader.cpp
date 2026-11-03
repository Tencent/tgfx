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

#include "gpu/shaders/kernels/NonAARRectFillShader.h"
#include <optional>
#include "gpu/ProgramInfo.h"
#include "gpu/ShaderMatchHelpers.h"
#include "gpu/aot/AOTChainBuilder.h"
#include "gpu/aot/DrawShape.h"
#include "gpu/processors/PorterDuffXferProcessor.h"
#include "gpu/processors/RRectEffect.h"
#include "gpu/processors/RectEffect.h"

namespace tgfx {

std::optional<NonAARRectFillShader::Inputs> NonAARRectFillShader::ExtractFacts(
    const DrawShape& facts, PermutationMatchFailure*) {
  // The geometry kind IS the contract: RRectDrawOp's non-AA form, no processor-tree walk.
  if (facts.geometry.kind != GeometryKind::NonAARRect) {
    return std::nullopt;
  }
  if (facts.fragmentCount > 1) {
    return std::nullopt;
  }
  Inputs inputs;
  inputs.isStroke = facts.geometry.isStroke;
  if (facts.fragmentCount == 1) {
    // A sole analytic clip (AA rect or rounded rect) rides the runtime clip contract uniforms, so
    // it needs no dimension: the FP is there only to write them.
    const bool clipOnly = facts.colorFPCount == 0 &&
                          (facts.coverage == Coverage::AARect || facts.coverage == Coverage::RRect);
    if (!clipOnly) {
      // The textured variant carries the single-tap tiled modes only: the mipmap-repeat 4-tap path
      // and perspective coordinates stay on the runtime route. The textured form never combines
      // with stroke; Compose applies that rejection through the shared predicate.
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
      if (!TiledModeSupported(static_cast<int>(leaf->tiledRecipe->shaderModeX)) ||
          !TiledModeSupported(static_cast<int>(leaf->tiledRecipe->shaderModeY))) {
        return std::nullopt;
      }
      inputs.textured = true;
    }
  }
  inputs.xpType = facts.xpType();
  return inputs;
}

}  // namespace tgfx

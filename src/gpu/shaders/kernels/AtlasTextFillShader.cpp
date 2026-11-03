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

#include "gpu/shaders/kernels/AtlasTextFillShader.h"
#include <optional>
#include "gpu/ProgramInfo.h"
#include "gpu/ShaderMatchHelpers.h"
#include "gpu/aot/DrawShape.h"
#include "gpu/processors/PorterDuffXferProcessor.h"

namespace tgfx {

std::optional<AtlasTextFillShader::Inputs> AtlasTextFillShader::ExtractFacts(
    const DrawShape& facts, PermutationMatchFailure*) {
  // The geometry kind IS the contract: AtlasTextOp's glyph processor, no processor-tree walk.
  if (facts.geometry.kind != GeometryKind::AtlasText) {
    return std::nullopt;
  }
  if (facts.colorFPCount != 0 || facts.fragmentCount > 1) {
    return std::nullopt;
  }
  const int xpType = facts.xpType();
  if (xpType < XP_NONE) {
    return std::nullopt;
  }
  if (facts.fragmentCount == 1) {
    // A single coverage FP multiplies the atlas glyph coverage: either a direct analytic clip
    // (AA rect / rrect) via the runtime clip contract, or an alpha-only affine device-space mask
    // — the mask sampler facts come off the IR's DeviceMask term.
    const bool directClip = facts.coverage == Coverage::AARect || facts.coverage == Coverage::RRect;
    if (!directClip) {
      auto* mask = DeviceMaskCoverageLeaf(facts.ir);
      if (mask == nullptr || !mask->isAlphaOnly || mask->hasPerspective) {
        return std::nullopt;
      }
    }
  }
  Inputs inputs;
  inputs.hasCoverage = facts.geometry.hasCoverage;
  inputs.xpType = xpType;
  return inputs;
}

}  // namespace tgfx

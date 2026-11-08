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

#include "gpu/shaders/kernels/AtlasTextFillShader.h"
#include "gpu/ProgramInfo.h"
#include "gpu/shaders/contract/DrawFacts.h"
#include "gpu/aot/select/ShaderMatchHelpers.h"

namespace tgfx {

bool AtlasTextFillShader::Accept(const DrawShape& shape) {
  // The geometry kind IS the contract: AtlasTextOp's glyph processor, no processor-tree walk.
  if (shape.geometry.kind != GeometryKind::AtlasText) {
    return false;
  }
  if (shape.colorFPCount != 0 || shape.fragmentCount > 1) {
    return false;
  }
  if (shape.xpType() < XP_NONE) {
    return false;
  }
  if (shape.fragmentCount == 1) {
    // A single coverage FP multiplies the atlas glyph coverage: either a direct analytic clip
    // (AA rect / rrect) via the runtime clip contract, or an alpha-only affine device-space mask
    // — the mask sampler facts come off the IR's DeviceMask term.
    const bool directClip = shape.coverage == Coverage::AARect || shape.coverage == Coverage::RRect;
    if (!directClip) {
      auto* mask = DeviceMaskCoverageLeaf(shape.ir);
      if (mask == nullptr || !mask->isAlphaOnly || mask->hasPerspective) {
        return false;
      }
    }
  }
  return true;
}

Variants AtlasTextFillShader::Select(const DrawShape& shape) {
  Variants variants;
  int hasCoverage = shape.geometry.hasCoverage ? 1 : 0;
  variants.vert("HAS_COVERAGE", hasCoverage)
      .frag("HAS_COVERAGE", hasCoverage)
      .frag(XP_DEFINE_NAME, shape.xpType());
  return variants;
}

}  // namespace tgfx

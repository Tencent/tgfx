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

#include "gpu/shaders/kernels/NonAARRectFillShader.h"
#include "gpu/ProgramInfo.h"
#include "gpu/aot/chain/AOTChainBuilder.h"
#include "gpu/aot/ir/DrawIR.h"
#include "gpu/aot/select/ShaderMatchHelpers.h"
#include "gpu/shaders/contract/DrawFacts.h"

namespace tgfx {

bool NonAARRectFillShader::Accept(const DrawShape& shape) {
  // The geometry kind IS the contract: RRectDrawOp's non-AA form, no processor-tree walk.
  if (shape.geometry.kind != GeometryKind::NonAARRect) {
    return false;
  }
  if (shape.fragmentCount > 1) {
    return false;
  }
  if (shape.fragmentCount == 1) {
    // A sole analytic clip (AA rect or rounded rect) rides the runtime clip contract uniforms, so
    // it needs no dimension: the FP is there only to write them.
    const bool clipOnly = shape.colorFPCount == 0 &&
                          (shape.coverage == Coverage::AARect || shape.coverage == Coverage::RRect);
    if (!clipOnly) {
      // The textured variant carries the single-tap tiled modes only: the mipmap-repeat 4-tap path
      // and perspective coordinates stay on the runtime route. The textured form never combines
      // with stroke; the `allowed` predicate applies that rejection.
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
      if (!TiledModeSupported(static_cast<int>(leaf->tiledRecipe->shaderModeX)) ||
          !TiledModeSupported(static_cast<int>(leaf->tiledRecipe->shaderModeY))) {
        return false;
      }
    }
  }
  return shape.xpType() >= XP_NONE;
}

Variants NonAARRectFillShader::Select(const DrawShape& shape) {
  // The textured flag mirrors the acceptance's structure: a non-clip sole FP is the tiled fill.
  const bool clipOnly = shape.colorFPCount == 0 &&
                        (shape.coverage == Coverage::AARect || shape.coverage == Coverage::RRect);
  int textured = shape.fragmentCount == 1 && !clipOnly ? 1 : 0;
  int isStroke = shape.geometry.isStroke ? 1 : 0;
  Variants variants;
  variants.vert("STROKE", isStroke)
      .vert("TEXTURED", textured)
      .frag("STROKE", isStroke)
      .frag("TEXTURED", textured)
      .frag(XP_DEFINE_NAME, shape.xpType());
  return variants;
}

}  // namespace tgfx

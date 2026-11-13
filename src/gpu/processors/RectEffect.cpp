/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "RectEffect.h"
#include "core/utils/Log.h"
#include "gpu/aot/ir/EffectGraph.h"
#include "gpu/shaders/KernelContract.h"
#include "gpu/shaders/modules/AnalyticClipModule.h"

namespace tgfx {

// Only needTransform is keyed. The transform path adds a per-fragment matrix multiply, so a
// dedicated program that skips this for the identity case is worth the extra program. The AA branch
// adds only a bounded amount of work that does not grow with the input, so keeping antiAlias a
// runtime uniform and sharing one program across AA and non-AA draws is a better trade-off than
// doubling the program count.
bool RectEffect::lowerToAOT(AOTNodeBuilder* builder, AOTNodeID input, AOTNodeID* output) const {
  if (builder == nullptr || output == nullptr) {
    return false;
  }
  const auto& matrix = deviceToLocal();
  if (!antiAlias) {
    // Reuse the rrect op's runtime antiAlias switch for a hard-edged rect. Zero radii make its
    // interior distance test identical to RectEffect's inclusive non-AA rectangle.
    AOTRRectCoverageParameters parameters = {};
    parameters.rect = {localRect.left, localRect.top, localRect.right, localRect.bottom};
    parameters.deviceToLocal = {matrix[0], matrix[1], matrix[2], matrix[3], matrix[4],
                                matrix[5], matrix[6], matrix[7], matrix[8]};
    parameters.antiAlias = 0.0f;
    return builder->addRRectCoverage(input, parameters, output);
  }
  AOTRectCoverageParameters parameters = {};
  parameters.rect = {localRect.left, localRect.top, localRect.right, localRect.bottom};
  parameters.deviceToLocal = {matrix[0], matrix[1], matrix[2], matrix[3], matrix[4],
                              matrix[5], matrix[6], matrix[7], matrix[8]};
  return builder->addRectCoverage(input, parameters, output);
}

PlacementPtr<RectEffect> RectEffect::Make(BlockAllocator* allocator, const Rect& localRect,
                                          const Matrix& matrix, bool antiAlias) {
  // The analytic AA relies on a linear device-to-local inverse map, which cannot represent a
  // perspective matrix.
  DEBUG_ASSERT(!matrix.hasPerspective());
  if (matrix.hasPerspective()) {
    return nullptr;
  }

  Rect adjustedRect = localRect;
  Matrix deviceToLocal = Matrix::I();
  if (!matrix.isIdentity()) {
    // Decompose matrix as M = M_axisFree * diag(axisScales). Bake axisScales into the local
    // rect so the deviceToLocal matrix used at runtime preserves the device-space half-pixel
    // AA band semantics.
    const auto scales = matrix.getAxisScales();
    if (scales.x == 0 || scales.y == 0) {
      return nullptr;
    }
    adjustedRect.scale(scales.x, scales.y);
    Matrix adjustedMatrix = matrix;
    adjustedMatrix.preScale(1.0f / scales.x, 1.0f / scales.y);
    if (!adjustedMatrix.invert(&deviceToLocal)) {
      return nullptr;
    }
  }
  return allocator->make<RectEffect>(adjustedRect, deviceToLocal, antiAlias);
}

}  // namespace tgfx

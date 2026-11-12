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

#include "RRectEffect.h"
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
void RRectEffect::onComputeProcessorKey(BytesKey* bytesKey) const {
  bytesKey->write(static_cast<uint32_t>(_needTransform ? 1 : 0));
}

bool RRectEffect::lowerToAOT(AOTNodeBuilder* builder, AOTNodeID input, AOTNodeID* output) const {
  // Every rrect form (device or local space, AA or hard edge) resolves through runtime uniforms,
  // so the lowering accepts them all; perspective and degenerate matrices were rejected at
  // construction.
  if (builder == nullptr || output == nullptr) {
    return false;
  }
  AOTRRectCoverageParameters parameters = {};
  parameters.rect = {localRect.left, localRect.top, localRect.right, localRect.bottom};
  for (size_t i = 0; i < 4; ++i) {
    parameters.radiiX[i] = radii[i].x;
    parameters.radiiY[i] = radii[i].y;
  }
  const auto& matrix = deviceToLocal();
  parameters.deviceToLocal = {matrix[0], matrix[1], matrix[2], matrix[3], matrix[4],
                              matrix[5], matrix[6], matrix[7], matrix[8]};
  parameters.antiAlias = antiAlias ? 1.0f : 0.0f;
  return builder->addRRectCoverage(input, parameters, output);
}

PlacementPtr<RRectEffect> RRectEffect::Make(BlockAllocator* allocator, const RRect& rRect,
                                            const Matrix& matrix, bool antiAlias) {
  DEBUG_ASSERT(rRect.type() != RRect::Type::Rect);
  // The analytic AA relies on a linear device-to-local inverse map, which cannot represent a
  // perspective matrix.
  DEBUG_ASSERT(!matrix.hasPerspective());
  if (matrix.hasPerspective()) {
    return nullptr;
  }

  RRect adjustedRRect = rRect;
  Matrix deviceToLocal = Matrix::I();
  if (!matrix.isIdentity()) {
    const auto scales = matrix.getAxisScales();
    if (scales.x == 0 || scales.y == 0) {
      return nullptr;
    }
    // Bake axisScales into the rrect so the shader's local-space distance comparison
    // matches device-space half-pixel AA semantics.
    adjustedRRect.scale(scales.x, scales.y);
    Matrix adjustedMatrix = matrix;
    adjustedMatrix.preScale(1.0f / scales.x, 1.0f / scales.y);
    if (!adjustedMatrix.invert(&deviceToLocal)) {
      return nullptr;
    }
  }
  return allocator->make<RRectEffect>(adjustedRRect.rect(), adjustedRRect.radii(), deviceToLocal,
                                      antiAlias);
}

}  // namespace tgfx

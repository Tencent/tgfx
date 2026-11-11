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

#include "GLSLRRectEffect.h"
#include "core/utils/Log.h"
#include "gpu/shaders/KernelContract.h"

namespace tgfx {

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
  return allocator->make<GLSLRRectEffect>(adjustedRRect.rect(), adjustedRRect.radii(),
                                          deviceToLocal, antiAlias);
}

GLSLRRectEffect::GLSLRRectEffect(const Rect& localRect, const std::array<Point, 4>& radii,
                                 const Matrix& deviceToLocal, bool antiAlias)
    : RRectEffect(localRect, radii, deviceToLocal, antiAlias) {
}

}  // namespace tgfx

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

#include "GLSLRectEffect.h"
#include "core/utils/Log.h"
#include "gpu/shaders/KernelContract.h"

namespace tgfx {

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
  return allocator->make<GLSLRectEffect>(adjustedRect, deviceToLocal, antiAlias);
}

GLSLRectEffect::GLSLRectEffect(const Rect& localRect, const Matrix& deviceToLocal, bool antiAlias)
    : RectEffect(localRect, deviceToLocal, antiAlias) {
}

void GLSLRectEffect::onSetData(UniformData*, UniformData* fragmentUniformData) const {
  fragmentUniformData->setData(ClipContract::LocalRect, localRect);
  const float antiAliasValue = antiAlias ? 1.0f : 0.0f;
  fragmentUniformData->setData(ClipContract::AntiAlias, antiAliasValue);
  if (needTransform()) {
    fragmentUniformData->setData(ClipContract::DeviceToLocal, deviceToLocal());
  }
  if (fragmentUniformData->hasField(ClipContract::Rect) && isDeviceSpaceRect()) {
    // The precompiled fill kernels evaluate a direct analytic clip through the shared
    // Rect/HasClip contract (clip_coverage.inc), which expects the device-space rect
    // uploaded half a pixel outward so the coverage ramp centers on the geometric boundary.
    fragmentUniformData->setData(ClipContract::Rect, localRect.makeOutset(0.5f, 0.5f));
  }
  if (fragmentUniformData->hasField(ClipContract::HasClip)) {
    // HasClip selects the contract branch: 1 = device-space AA rect (Rect with the half-pixel
    // outset above), 3 = local-space AA rect (LocalRect/DeviceToLocal, written by the JIT-path
    // uploads above with the exact local rect and the device-to-local matrix).
    int hasClip = isDeviceSpaceRect() ? 1 : 3;
    fragmentUniformData->setData(ClipContract::HasClip, hasClip);
  }
}

}  // namespace tgfx

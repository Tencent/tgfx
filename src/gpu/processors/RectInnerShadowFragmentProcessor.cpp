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

#include "gpu/processors/RectInnerShadowFragmentProcessor.h"

namespace tgfx {

RectInnerShadowFragmentProcessor::RectInnerShadowFragmentProcessor(const Point& shadowHalfSize,
                                                                   const Point& maskHalfOverSigma,
                                                                   PMColor color,
                                                                   const Matrix& shadowCoordMatrix,
                                                                   const Matrix& maskCoordMatrix)
    : FragmentProcessor(ClassID()), shadowHalfSize(shadowHalfSize),
      maskHalfOverSigma(maskHalfOverSigma), color(color), shadowCoordTransform(shadowCoordMatrix),
      maskCoordTransform(maskCoordMatrix) {
  addCoordTransform(&shadowCoordTransform);
  addCoordTransform(&maskCoordTransform);
}

void RectInnerShadowFragmentProcessor::onComputeProcessorKey(BytesKey* bytesKey) const {
  // Geometry and color are uniforms, so they do not affect the program. Each coord transform's
  // perspective property decides whether the shader emits the perspective division for that
  // coordinate, so both go into the key.
  uint32_t flags = shadowCoordTransform.matrix.hasPerspective() ? 1 : 0;
  flags |= maskCoordTransform.matrix.hasPerspective() ? 2 : 0;
  bytesKey->write(flags);
}

}  // namespace tgfx

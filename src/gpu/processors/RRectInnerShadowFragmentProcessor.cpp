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

#include "gpu/processors/RRectInnerShadowFragmentProcessor.h"

namespace tgfx {

RRectInnerShadowFragmentProcessor::RRectInnerShadowFragmentProcessor(
    const Point& shadowHalfOverSigma, const Point& shadowCornerOverSigma,
    const Point& shadowCenterOffset, const Point& invSigma, const Point& maskHalfSize,
    float maskCornerRadius, PMColor color, const Matrix* uvMatrix)
    : FragmentProcessor(ClassID()), shadowHalfOverSigma(shadowHalfOverSigma),
      shadowCornerOverSigma(shadowCornerOverSigma), shadowCenterOffset(shadowCenterOffset),
      invSigma(invSigma), maskHalfSize(maskHalfSize), maskCornerRadius(maskCornerRadius),
      color(color) {
  if (uvMatrix) {
    coordTransform = CoordTransform(*uvMatrix);
  }
  addCoordTransform(&coordTransform);
}

void RRectInnerShadowFragmentProcessor::onComputeProcessorKey(BytesKey*) const {
  // Geometry and color are uniforms, so all instances share one program.
}
}  // namespace tgfx

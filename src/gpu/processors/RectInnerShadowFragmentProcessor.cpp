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

RectInnerShadowFragmentProcessor::RectInnerShadowFragmentProcessor(const Point& maskHalfOverSigma,
                                                                   const Point& shadowHalfSize,
                                                                   PMColor color,
                                                                   const Matrix& shadowCoordMatrix,
                                                                   const Matrix& maskCoordMatrix)
    : FragmentProcessor(ClassID()), maskHalfOverSigma(maskHalfOverSigma),
      shadowHalfSize(shadowHalfSize), color(color), shadowCoordTransform(shadowCoordMatrix),
      maskCoordTransform(maskCoordMatrix) {
  addCoordTransform(&shadowCoordTransform);
  addCoordTransform(&maskCoordTransform);
}

void RectInnerShadowFragmentProcessor::onComputeProcessorKey(BytesKey*) const {
  // Geometry and color are uniforms, so all instances share one program.
}
}  // namespace tgfx

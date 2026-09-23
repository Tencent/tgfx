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
    const Point& maskHalfOverSigma, const Point& maskCornerOverSigma, const Point& shadowHalfSize,
    const Point& shadowCornerRadius, PMColor color, const Matrix& shadowCoordMatrix,
    const Matrix& maskCoordMatrix, int quadratureCount)
    : FragmentProcessor(ClassID()), maskHalfOverSigma(maskHalfOverSigma),
      maskCornerOverSigma(maskCornerOverSigma), shadowHalfSize(shadowHalfSize),
      shadowCornerRadius(shadowCornerRadius), color(color), quadratureCount(quadratureCount),
      shadowCoordTransform(shadowCoordMatrix), maskCoordTransform(maskCoordMatrix) {
  addCoordTransform(&shadowCoordTransform);
  addCoordTransform(&maskCoordTransform);
}

void RRectInnerShadowFragmentProcessor::onComputeProcessorKey(BytesKey* bytesKey) const {
  // Geometry and color are uniforms, so they do not affect the program. The quadrature count is
  // baked into the shader source, so instances differing in it need separate programs.
  bytesKey->write(static_cast<uint32_t>(quadratureCount));
}
}  // namespace tgfx

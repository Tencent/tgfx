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

#include "gpu/processors/RRectBlurFragmentProcessor.h"
#include <algorithm>

namespace tgfx {

// Above this ratio between the two normalized corner semi-axes the low count no longer keeps the
// error within what the high count reaches elsewhere. Widening it to 2.5 already lets a shape reach
// 8.15 levels against the 7.90 the high count gives at its own worst.
static constexpr float SemiAxisRatioThreshold = 2.0f;

int SelectRRectBlurQuadratureCount(const Point& cornerOverSigma) {
  if (cornerOverSigma.x <= 0.0f || cornerOverSigma.y <= 0.0f) {
    return RRectBlurLowQuadratureCount;
  }
  const auto ratio =
      std::max(cornerOverSigma.x / cornerOverSigma.y, cornerOverSigma.y / cornerOverSigma.x);
  return ratio > SemiAxisRatioThreshold ? RRectBlurHighQuadratureCount
                                        : RRectBlurLowQuadratureCount;
}

RRectBlurFragmentProcessor::RRectBlurFragmentProcessor(const Point& halfSizeOverSigma,
                                                       const Point& cornerOverSigma, PMColor color,
                                                       const Matrix& coordMatrix,
                                                       int quadratureCount)
    : FragmentProcessor(ClassID()), halfSizeOverSigma(halfSizeOverSigma),
      cornerOverSigma(cornerOverSigma), color(color), quadratureCount(quadratureCount),
      coordTransform(coordMatrix) {
  addCoordTransform(&coordTransform);
}

void RRectBlurFragmentProcessor::onComputeProcessorKey(BytesKey* bytesKey) const {
  // Geometry and color are uniforms, so they do not affect the program. The quadrature count is
  // baked into the shader source, so instances differing in it need separate programs.
  bytesKey->write(static_cast<uint32_t>(quadratureCount));
}
}  // namespace tgfx

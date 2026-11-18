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

#include "layers/processors/RectBlurFragmentProcessor.h"

namespace tgfx {

RectBlurFragmentProcessor::RectBlurFragmentProcessor(const Point& halfSizeOverSigma, PMColor color,
                                                     const Matrix& coordMatrix)
    : FragmentProcessor(ClassID()), halfSizeOverSigma(halfSizeOverSigma), color(color),
      coordTransform(coordMatrix) {
  addCoordTransform(&coordTransform);
}

void RectBlurFragmentProcessor::onComputeProcessorKey(BytesKey* bytesKey) const {
  // Geometry and color are uniforms, so they do not affect the program. The coord transform's
  // perspective property decides whether the shader emits the perspective division, so it goes
  // into the key.
  const uint32_t flags = coordTransform.matrix.hasPerspective() ? 1 : 0;
  bytesKey->write(flags);
}

}  // namespace tgfx

/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "GLSLGaussianBlur1DFragmentProcessor.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include "gpu/processors/TextureEffect.h"
#include "gpu/processors/TiledTextureEffect.h"

namespace tgfx {

PlacementPtr<FragmentProcessor> GaussianBlur1DFragmentProcessor::Make(
    BlockAllocator* allocator, PlacementPtr<FragmentProcessor> processor, float sigma,
    GaussianBlurDirection direction, float stepLength, int maxSigma) {
  if (!processor) {
    return nullptr;
  }
  if (maxSigma < 0) {
    return nullptr;
  }
  if (sigma <= 0 || stepLength <= 0 || !std::isfinite(sigma)) {
    return processor;
  }
  // The kernel table and the shader loop bound are dimensioned for maxSigma and sigma, so the
  // caller must respect both limits. Clamping here would silently mask contract violations.
  DEBUG_ASSERT(maxSigma <= MAX_KERNEL_RADIUS / 2);
  DEBUG_ASSERT(sigma <= static_cast<float>(maxSigma));

  return allocator->make<GLSLGaussianBlur1DFragmentProcessor>(std::move(processor), sigma,
                                                              direction, stepLength, maxSigma);
}

GLSLGaussianBlur1DFragmentProcessor::GLSLGaussianBlur1DFragmentProcessor(
    PlacementPtr<FragmentProcessor> processor, float sigma, GaussianBlurDirection direction,
    float stepLength, int maxSigma)
    : GaussianBlur1DFragmentProcessor(std::move(processor), sigma, direction, stepLength,
                                      maxSigma) {
}

}  // namespace tgfx

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

#include "GaussianBlur1DFragmentProcessor.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace tgfx {

GaussianBlur1DFragmentProcessor::GaussianBlur1DFragmentProcessor(
    PlacementPtr<FragmentProcessor> processor, float sigma, GaussianBlurDirection direction,
    float stepLength, int maxSigma)
    : Blur1DFragmentProcessor(ClassID()), sigma(sigma), direction(direction),
      stepLength(stepLength), maxSigma(maxSigma) {
  registerChildProcessor(std::move(processor));
  computeKernel();
}

void GaussianBlur1DFragmentProcessor::computeKernel() {
  // Clamp sigma defensively because the kernel table is dimensioned for maxSigma. Clamping sigma
  // rather than only the derived radius keeps the kernel weights consistent with the clamped value.
  // Non-finite or non-positive sigma is reset to zero so that ceil() and the exp() below stay
  // defined: with sigma = 0 the denominator is clamped to 1 and the kernel degenerates to a single
  // center sample, which means no blurring.
  sigma = std::min(sigma, static_cast<float>(maxSigma));
  if (!std::isfinite(sigma) || sigma <= 0.0f) {
    sigma = 0.0f;
  }
  kernelRadius = static_cast<int>(ceil(2.0f * sigma));
  // Clamp by the sample count so the total number of samples (2 * radius + 1) never exceeds the
  // kernel table capacity.
  kernelRadius = std::min(kernelRadius, MAX_KERNEL_RADIUS);
  // Guard against a zero denominator when sigma is zero: with sigma = 0 the kernel degenerates to
  // a single center sample. The floor keeps the weights exact for all valid sigma values.
  const float denominator = std::max(2.0f * sigma * sigma, std::numeric_limits<float>::min());
  float total = 0.0f;
  for (int i = 0; i <= kernelRadius; ++i) {
    const float weight = exp(-static_cast<float>(i * i) / denominator);
    kernel[static_cast<size_t>(i)] = weight;
    // The kernel is symmetric, so each off-center weight contributes twice to the total.
    total += (i == 0) ? weight : 2.0f * weight;
  }
  for (int i = 0; i <= kernelRadius; ++i) {
    kernel[static_cast<size_t>(i)] /= total;
  }
}

void GaussianBlur1DFragmentProcessor::onComputeProcessorKey(BytesKey* key) const {
  key->write(maxSigma);
}

}  // namespace tgfx

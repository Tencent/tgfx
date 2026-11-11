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
#include "gpu/aot/ir/EffectGraph.h"

namespace tgfx {

GaussianBlur1DFragmentProcessor::GaussianBlur1DFragmentProcessor(
    PlacementPtr<FragmentProcessor> processor, float sigma, GaussianBlurDirection direction,
    float stepLength, int maxSigma)
    : Blur1DFragmentProcessor(ClassID()), direction(direction), stepLength(stepLength),
      maxSigma(maxSigma) {
  registerChildProcessor(std::move(processor));
  computeKernel(sigma);
}

void GaussianBlur1DFragmentProcessor::computeKernel(float sigma) {
  // Make() guarantees in debug builds that sigma is finite, positive, and not greater than
  // maxSigma, so no clamping is needed here. In release builds the DEBUG_ASSERT is compiled out
  // and the same contract is upheld by the only call site, GaussianBlurImageFilter, which clamps
  // sigma before construction. The radius clamp below is a defensive guard for the kernel table
  // capacity that never triggers while the contract holds.
  kernelRadius = static_cast<int>(ceil(2.0f * sigma));
  // Clamp by the sample count so the total number of samples (2 * radius + 1) never exceeds the
  // kernel table capacity.
  kernelRadius = std::min(kernelRadius, MAX_KERNEL_RADIUS);
  // Guard against a zero denominator: the product 2 * sigma * sigma can underflow to zero even
  // when sigma itself is a valid positive float, in which case the kernel degenerates to a single
  // center sample, which means no blurring.
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

bool GaussianBlur1DFragmentProcessor::lowerToAOT(AOTNodeBuilder* builder, AOTNodeID input,
                                                 AOTNodeID* output) const {
  // The blur's math cannot ride the pointwise chain, so it lowers as an opaque-kernel barrier:
  // the dedicated family's kernel runs it (the planner refuses the graph, keeping the draw on
  // the plain route), and the family's IR-based matching reads the node. The child
  // lowers normally — its texture leaf is the natural cut point for a future split (a color
  // grade under the blur materializes as its own pass).
  if (builder == nullptr || output == nullptr || numChildProcessors() != 1) {
    return false;
  }
  AOTNodeID childNode = AOTNodeID::Invalid();
  if (!childProcessor(0)->lowerToAOT(builder, input, &childNode)) {
    return false;
  }
  AOTOpaqueKernelParameters parameters = {};
  parameters.kernel = AOTOpaqueKernel::GaussianBlur1D;
  // The fixed kernel caps sigma at 10; the matcher reads the acceptance range off the node.
  parameters.sigma = maxSigma;
  // The upload values the blur bind reads off the node (see AOTOpaqueKernelParameters). The
  // step's matrix is NOT stashed here: its normalization needs the child's instantiated view,
  // so the bind maps it through the child leaf's recorded matrix at bind time.
  parameters.kernelRadius = getKernelRadius();
  parameters.stepLength = getStepLength();
  parameters.vertical = getDirection() == GaussianBlurDirection::Vertical;
  parameters.packedKernel = packedKernel();
  return builder->addOpaqueKernel(childNode, parameters, output);
}
}  // namespace tgfx

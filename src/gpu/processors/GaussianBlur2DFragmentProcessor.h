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

#pragma once

#include <array>
#include "gpu/processors/FragmentProcessor.h"

namespace tgfx {

/**
 * Blurs its child with a gaussian in a single pass by sampling the full (2 * radiusX + 1) x
 * (2 * radiusY + 1) neighbourhood. The weights are the product of the two half-kernels the
 * separable blur would use, so the result matches the two-pass blur up to the rounding of its
 * intermediate target. It only pays off for small kernels, where the extra samples cost less than
 * the render pass they save.
 */
class GaussianBlur2DFragmentProcessor : public FragmentProcessor {
 public:
  // The largest sigma the single pass supports. Its kernel radius ceil(2 * sigma) bounds the
  // shader loops, so a pixel takes at most (2 * MAX_RADIUS + 1)^2 samples.
  static constexpr float MAX_SIGMA = 2.0f;
  static constexpr int MAX_RADIUS = 4;
  static constexpr int KERNEL_SIZE = MAX_RADIUS + 1;
  static constexpr int KERNEL_VEC4_COUNT = (KERNEL_SIZE + 3) / 4;

  /**
   * Creates the processor. Pixel (x, y) of the target reads the child around (x, y) + origin,
   * where origin is measured in target pixels. Both sigmas must lie in [0, MAX_SIGMA] and at least
   * one must be positive; returns nullptr otherwise or when the child is null.
   */
  static PlacementPtr<FragmentProcessor> Make(BlockAllocator* allocator,
                                              PlacementPtr<FragmentProcessor> processor,
                                              float sigmaX, float sigmaY, const Point& origin);

  std::string name() const override {
    return "GaussianBlur2DFragmentProcessor";
  }

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  GaussianBlur2DFragmentProcessor(PlacementPtr<FragmentProcessor> processor, float sigmaX,
                                  float sigmaY, const Point& origin);

  Point origin = {};
  int radiusX = 0;
  int radiusY = 0;
  // Normalized half-kernels, see GaussianBlur1DFragmentProcessor::ComputeHalfKernel().
  std::array<float, KERNEL_SIZE> kernelX = {};
  std::array<float, KERNEL_SIZE> kernelY = {};
};
}  // namespace tgfx

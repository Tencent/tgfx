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

#pragma once

#include "gpu/processors/Blur1DFragmentProcessor.h"

namespace tgfx {

enum class GaussianBlurDirection { Horizontal, Vertical };

class GaussianBlur1DFragmentProcessor : public Blur1DFragmentProcessor {
 public:
  /**
   * Creates a gaussian blur processor that samples the child along the given direction with the
   * specified step. sigma is the standard deviation of the gaussian kernel in pixels. Returns
   * nullptr when the processor or maxSigma is invalid, otherwise returns the child processor
   * unchanged when sigma or stepLength is not positive.
   */
  static PlacementPtr<FragmentProcessor> Make(BlockAllocator* allocator,
                                              PlacementPtr<FragmentProcessor> processor,
                                              float sigma, GaussianBlurDirection direction,
                                              float stepLength, int maxSigma);

  std::string name() const override {
    return "GaussianBlur1DFragmentProcessor";
  }

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  GaussianBlur1DFragmentProcessor(PlacementPtr<FragmentProcessor> processor, float sigma,
                                  GaussianBlurDirection direction, float stepLength, int maxSigma);

  void onComputeProcessorKey(BytesKey* key) const override;

  // The direction in which the blur is applied.
  GaussianBlurDirection direction = GaussianBlurDirection::Horizontal;
  // The pixel offset between adjacent samples.
  float stepLength = 1.f;
  // The maximum allowed sigma, bounding the shader loop and the kernel table size.
  int maxSigma = 10;

  void computeKernel(float strength) override;
  int kernelLoopUpperBound() const override {
    return 4 * maxSigma;
  }
};
}  // namespace tgfx

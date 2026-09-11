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

#include "gpu/processors/FragmentProcessor.h"
#include "tgfx/core/Color.h"

namespace tgfx {

/**
 * Computes the coverage of an axis-aligned rectangle convolved with a Gaussian kernel truncated at
 * +/- 2 sigma, evaluated in closed form. The rectangle indicator is separable, so the coverage is
 * the product of one CDF difference per axis and no sampling is required.
 *
 * All geometry is pre-divided by sigma on the CPU side, so the kernel is a unit Gaussian in shader
 * space and there is no upper bound on the blur radius. Anisotropic blur is supported by dividing
 * each axis by its own sigma.
 */
class RectBlurFragmentProcessor : public FragmentProcessor {
 public:
  /**
   * Creates a processor that fills with color modulated by the blurred rectangle coverage.
   * @param allocator the allocator used to create the processor.
   * @param halfSizeOverSigma half the rectangle size, each axis divided by that axis' sigma.
   * @param color the shadow color, premultiplied.
   * @param uvMatrix maps local coordinates to the sigma-normalized space centered on the rect.
   */
  static PlacementPtr<FragmentProcessor> Make(BlockAllocator* allocator,
                                              const Point& halfSizeOverSigma, PMColor color,
                                              const Matrix* uvMatrix);

  std::string name() const override {
    return "RectBlurFragmentProcessor";
  }

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  RectBlurFragmentProcessor(const Point& halfSizeOverSigma, PMColor color, const Matrix* uvMatrix);

  void onComputeProcessorKey(BytesKey* bytesKey) const override;

  Point halfSizeOverSigma = {};
  PMColor color = {};
  CoordTransform coordTransform;
};
}  // namespace tgfx

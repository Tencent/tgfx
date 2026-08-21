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
 * Computes the coverage of a rounded rectangle convolved with a Gaussian kernel truncated at
 * +/- 2 sigma. Rows are still symmetric intervals, so the horizontal integral stays closed form
 * while the vertical one uses a fixed-count midpoint quadrature: the cost is constant regardless of
 * blur radius and there is no upper bound on it.
 *
 * All geometry is pre-divided by sigma on the CPU side. Because each axis is divided by its own
 * sigma, an originally circular corner becomes elliptical, which is why the corner is described by
 * two semi-axes rather than one radius; they coincide when sigma is isotropic.
 */
class RRectBlurFragmentProcessor : public FragmentProcessor {
 public:
  /**
   * Creates a processor that fills with color modulated by the blurred rounded-rect coverage.
   * @param allocator the allocator used to create the processor.
   * @param halfSizeOverSigma half the bounds size, each axis divided by that axis' sigma.
   * @param cornerOverSigma the corner radius, each axis divided by that axis' sigma. Each
   * component must not exceed the matching halfSizeOverSigma component.
   * @param color the shadow color, premultiplied.
   * @param uvMatrix maps local coordinates to the sigma-normalized space centered on the bounds.
   */
  static PlacementPtr<FragmentProcessor> Make(BlockAllocator* allocator,
                                              const Point& halfSizeOverSigma,
                                              const Point& cornerOverSigma, PMColor color,
                                              const Matrix* uvMatrix);

  std::string name() const override {
    return "RRectBlurFragmentProcessor";
  }

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  RRectBlurFragmentProcessor(const Point& halfSizeOverSigma, const Point& cornerOverSigma,
                             PMColor color, const Matrix* uvMatrix);

  void onComputeProcessorKey(BytesKey* bytesKey) const override;

  Point halfSizeOverSigma = {};
  Point cornerOverSigma = {};
  PMColor color = {};
  CoordTransform coordTransform;
};
}  // namespace tgfx

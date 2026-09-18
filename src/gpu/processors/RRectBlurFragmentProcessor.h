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
 * Sample counts the rounded-rect coverage form uses along the axis it does not solve in closed form.
 * Dividing each axis by its own sigma turns a circular corner into an elliptical one, and the error
 * grows with how much the two semi-axes differ, so the count is picked per shape rather than fixed.
 * Measured worst cases over a space of normalized shapes, in 8-bit levels: 16.00 at 4 everywhere,
 * 7.90 at 8 everywhere, and 6.09 at 4 below the threshold with 8 above it.
 */
static constexpr int RRectBlurLowQuadratureCount = 4;
static constexpr int RRectBlurHighQuadratureCount = 8;

/**
 * Returns the sample count to use for a corner whose two semi-axes are given in sigma units. A
 * corner with no arc leaves the row half-width constant along the axis being summed, which the
 * midpoint rule integrates exactly, so it takes the low count.
 */
int SelectRRectBlurQuadratureCount(const Point& cornerOverSigma);

/**
 * Fills with a solid color modulated by the coverage of a rounded rectangle convolved with a
 * Gaussian kernel truncated at +/- 2 sigma. One axis is evaluated in closed form while the other is
 * summed over a fixed number of rows, so the cost does not depend on sigma.
 */
class RRectBlurFragmentProcessor : public FragmentProcessor {
 public:
  /**
   * Creates a processor that fills the given rounded rectangle with the given color and blurs it.
   * @param halfSizeOverSigma half the bounds size, each axis divided by its own sigma. Both
   * components must be positive.
   * @param cornerOverSigma the corner radius, each axis divided by its own sigma. Each component
   * must not exceed the matching halfSizeOverSigma component.
   * @param color the fill color, premultiplied.
   * @param coordMatrix maps the drawn coordinates into the space where the bounds are centered on
   * the origin and both axes are divided by sigma.
   * @param quadratureCount the number of sample points the coverage form uses along the axis it
   * does not solve in closed form. Must be positive. It is part of the generated shader, so
   * instances differing in it compile separate programs.
   */
  static PlacementPtr<FragmentProcessor> Make(BlockAllocator* allocator,
                                              const Point& halfSizeOverSigma,
                                              const Point& cornerOverSigma, PMColor color,
                                              const Matrix& coordMatrix, int quadratureCount);

  std::string name() const override {
    return "RRectBlurFragmentProcessor";
  }

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  RRectBlurFragmentProcessor(const Point& halfSizeOverSigma, const Point& cornerOverSigma,
                             PMColor color, const Matrix& coordMatrix, int quadratureCount);

  void onComputeProcessorKey(BytesKey* bytesKey) const override;

  Point halfSizeOverSigma = {};
  Point cornerOverSigma = {};
  PMColor color = {};
  int quadratureCount = 0;
  CoordTransform coordTransform;
};
}  // namespace tgfx

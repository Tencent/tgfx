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
 * Fills with a solid color modulated by the coverage of a rectangle convolved with a Gaussian
 * kernel truncated at +/- 2 sigma, evaluated in closed form, so its cost does not depend on sigma.
 */
class RectBlurFragmentProcessor : public FragmentProcessor {
 public:
  /**
   * Creates a processor that fills the given rectangle with the given color and blurs it.
   * @param halfSizeOverSigma half the rectangle size, each axis divided by its own sigma. Both
   * components must be positive.
   * @param color the fill color, premultiplied.
   * @param coordMatrix maps the drawn coordinates into the space where the rectangle is centered on
   * the origin and both axes are divided by sigma.
   */
  static PlacementPtr<FragmentProcessor> Make(BlockAllocator* allocator,
                                              const Point& halfSizeOverSigma, PMColor color,
                                              const Matrix& coordMatrix);

  std::string name() const override {
    return "RectBlurFragmentProcessor";
  }

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  RectBlurFragmentProcessor(const Point& halfSizeOverSigma, PMColor color,
                            const Matrix& coordMatrix);

  void onComputeProcessorKey(BytesKey* bytesKey) const override;

  Point halfSizeOverSigma = {};
  PMColor color = {};
  CoordTransform coordTransform;
};
}  // namespace tgfx

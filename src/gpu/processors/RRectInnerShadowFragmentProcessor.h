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
 * Fills with the inner shadow of a rounded rectangle in the given color: the light passes through a
 * second, inset rounded rectangle, so the shadow is what that shape's blurred coverage leaves
 * behind, kept only inside the outer shape.
 *
 * The shape and the blur are measured separately: the shape stays in content pixels so its
 * antialiased edge is one pixel wide whatever the blur is, and the offset applies to the shadow
 * alone, leaving the shape in place.
 */
class RRectInnerShadowFragmentProcessor : public FragmentProcessor {
 public:
  /**
   * Creates a processor that draws the inner shadow the inset shadow rounded rectangle casts
   * inside the mask rounded rectangle, in the given color.
   * @param shadowHalfOverSigma half the shadow bounds size, each axis divided by its own sigma.
   * Both components must be positive.
   * @param shadowCornerOverSigma the shadow corner radius, each axis divided by its own sigma. Each
   * component must not exceed the matching shadowHalfOverSigma component.
   * @param maskHalfSize half the mask bounds size, in content pixels. Both components must be
   * positive.
   * @param maskCornerRadius the mask corner radius per axis, in content pixels. The two components
   * may differ, describing an elliptical corner. Each component must not exceed the matching
   * maskHalfSize component.
   * @param color the fill color, premultiplied.
   * @param maskCoordMatrix maps the drawn coordinates into content pixels centered on the mask.
   * @param shadowCoordMatrix maps the drawn coordinates into the space where the shadow bounds are
   * centered on the origin and both axes are divided by sigma.
   * @param quadratureCount the number of sample points the blurred coverage uses along the axis it
   * does not solve in closed form. Must be positive. It is part of the generated shader, so
   * instances differing in it compile separate programs.
   */
  static PlacementPtr<FragmentProcessor> Make(BlockAllocator* allocator,
                                              const Point& shadowHalfOverSigma,
                                              const Point& shadowCornerOverSigma,
                                              const Point& maskHalfSize,
                                              const Point& maskCornerRadius, PMColor color,
                                              const Matrix& maskCoordMatrix,
                                              const Matrix& shadowCoordMatrix, int quadratureCount);

  std::string name() const override {
    return "RRectInnerShadowFragmentProcessor";
  }

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  RRectInnerShadowFragmentProcessor(const Point& shadowHalfOverSigma,
                                    const Point& shadowCornerOverSigma, const Point& maskHalfSize,
                                    const Point& maskCornerRadius, PMColor color,
                                    const Matrix& maskCoordMatrix, const Matrix& shadowCoordMatrix,
                                    int quadratureCount);

  void onComputeProcessorKey(BytesKey* bytesKey) const override;

  Point shadowHalfOverSigma = {};
  Point shadowCornerOverSigma = {};
  Point maskHalfSize = {};
  Point maskCornerRadius = {};
  PMColor color = {};
  int quadratureCount = 0;
  CoordTransform maskCoordTransform;
  CoordTransform shadowCoordTransform;
};
}  // namespace tgfx

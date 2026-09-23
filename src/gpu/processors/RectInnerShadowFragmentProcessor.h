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
 * Fills with the inner shadow of a rectangle in the given color: the mask is a second, inset
 * rectangle, so the shadow is what its blurred coverage leaves behind, kept only inside the shadow
 * shape.
 *
 * The shadow shape and the blur are measured separately: the shadow shape stays in content pixels
 * so its antialiased edge is one pixel wide whatever the blur is, and the offset applies to the
 * mask alone, leaving the shadow shape in place.
 */
class RectInnerShadowFragmentProcessor : public FragmentProcessor {
 public:
  /**
   * Creates a processor that draws the inner shadow left inside the shadow rectangle by the inset
   * mask rectangle's blurred coverage, in the given color.
   * @param maskHalfOverSigma half the mask rectangle size, each axis divided by its own sigma. Both
   * components must be positive.
   * @param shadowHalfSize half the shadow rectangle size, in content pixels. Both components must
   * be positive.
   * @param color the fill color, premultiplied.
   * @param shadowCoordMatrix maps the drawn coordinates into content pixels centered on the shadow
   * rectangle.
   * @param maskCoordMatrix maps the drawn coordinates into the space where the mask rectangle is
   * centered on the origin and both axes are divided by sigma.
   */
  static PlacementPtr<FragmentProcessor> Make(BlockAllocator* allocator,
                                              const Point& maskHalfOverSigma,
                                              const Point& shadowHalfSize, PMColor color,
                                              const Matrix& shadowCoordMatrix,
                                              const Matrix& maskCoordMatrix);

  std::string name() const override {
    return "RectInnerShadowFragmentProcessor";
  }

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  RectInnerShadowFragmentProcessor(const Point& maskHalfOverSigma, const Point& shadowHalfSize,
                                   PMColor color, const Matrix& shadowCoordMatrix,
                                   const Matrix& maskCoordMatrix);

  void onComputeProcessorKey(BytesKey* bytesKey) const override;

  Point maskHalfOverSigma = {};
  Point shadowHalfSize = {};
  PMColor color = {};
  CoordTransform shadowCoordTransform;
  CoordTransform maskCoordTransform;
};
}  // namespace tgfx

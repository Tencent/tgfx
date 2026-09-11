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
 * Computes an inner shadow for a rounded rectangle: the shadow is the complement of the blurred
 * coverage of a second rounded rectangle, clipped to the shape itself. The blurred coverage uses the
 * same closed form the outer shadow does, with the inner integral solved per row and the outer one
 * approximated by a fixed-count quadrature.
 *
 * Two coordinate spaces are involved. The mask is evaluated in content pixels because its
 * antialiased edge is one pixel wide regardless of sigma, while the shadow is evaluated in
 * sigma-normalized space. A single coordinate transform produces the former and the shader derives
 * the latter, which also accommodates the shadow offset moving only the shadow and not the mask.
 */
class RRectInnerShadowFragmentProcessor : public FragmentProcessor {
 public:
  /**
   * Creates a processor that fills with color modulated by the inner shadow coverage.
   * @param allocator the allocator used to create the processor.
   * @param shadowHalfOverSigma half the shadow bounds size, each axis divided by that axis' sigma.
   * @param shadowCornerOverSigma the shadow corner radius, each axis divided by that axis' sigma.
   * Each component must not exceed the matching shadowHalfOverSigma component.
   * @param shadowCenterOffset the shadow center relative to the mask center, in content pixels.
   * @param invSigma the reciprocal of sigma per axis, converting content pixels to shadow space.
   * @param maskHalfSize half the mask bounds size, in content pixels.
   * @param maskCornerRadius the mask corner radius per axis, in content pixels. The two
   * components may differ, describing an elliptical corner.
   * @param color the shadow color, premultiplied.
   * @param uvMatrix maps local coordinates to content pixels centered on the mask.
   */
  static PlacementPtr<FragmentProcessor> Make(BlockAllocator* allocator,
                                              const Point& shadowHalfOverSigma,
                                              const Point& shadowCornerOverSigma,
                                              const Point& shadowCenterOffset,
                                              const Point& invSigma, const Point& maskHalfSize,
                                              const Point& maskCornerRadius, PMColor color,
                                              const Matrix* uvMatrix);

  std::string name() const override {
    return "RRectInnerShadowFragmentProcessor";
  }

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  RRectInnerShadowFragmentProcessor(const Point& shadowHalfOverSigma,
                                    const Point& shadowCornerOverSigma,
                                    const Point& shadowCenterOffset, const Point& invSigma,
                                    const Point& maskHalfSize, const Point& maskCornerRadius,
                                    PMColor color, const Matrix* uvMatrix);

  void onComputeProcessorKey(BytesKey* bytesKey) const override;

  Point shadowHalfOverSigma = {};
  Point shadowCornerOverSigma = {};
  Point shadowCenterOffset = {};
  Point invSigma = {};
  Point maskHalfSize = {};
  Point maskCornerRadius = {};
  PMColor color = {};
  CoordTransform coordTransform;
};
}  // namespace tgfx

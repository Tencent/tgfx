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
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Point.h"
#include "tgfx/core/RRect.h"

namespace tgfx {

/**
 * Coverage FragmentProcessor for an affine-mapped rounded rectangle clip, in either AA or NonAA
 * mode. Accepts RRects of type Oval, Simple, or Complex; for Rect type, prefer RectEffect for
 * better performance. Perspective and degenerate matrices are not supported. Smoothed corners
 * are evaluated analytically only when they are circular, so RRects with corner smoothness and
 * elliptical corners (or a non-uniformly scaling matrix) are rejected.
 */
class RRectEffect : public FragmentProcessor {
 public:
  /**
   * @param rRect       Rounded rectangle in local space. For Rect type, prefer RectEffect for
   *                    better performance.
   * @param matrix      Maps the local-space rRect to device space. Defaults to identity.
   * @param antiAlias   If true, the shader produces a soft edge that ramps over one pixel centered
   *                    on the boundary; otherwise a hard edge.
   *
   * Returns nullptr when the matrix has perspective or its scale components are zero, or when the
   * rRect has corner smoothness that cannot be evaluated analytically (elliptical corners, or a
   * matrix whose axis scales differ).
   */
  static PlacementPtr<RRectEffect> Make(BlockAllocator* allocator, const RRect& rRect,
                                        const Matrix& matrix = Matrix::I(), bool antiAlias = true);

  std::string name() const override {
    return "RRectEffect";
  }

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  RRectEffect(const Rect& localRect, const std::array<Point, 4>& radii, float smoothness,
              const Matrix& deviceToLocal, bool antiAlias)
      : FragmentProcessor(ClassID()), localRect(localRect), radii(radii), smoothness(smoothness),
        antiAlias(antiAlias), _deviceToLocal(deviceToLocal),
        _needTransform(!deviceToLocal.isIdentity()), _hasSmoothness(smoothness > 0.0f) {
  }

  void onComputeProcessorKey(BytesKey* bytesKey) const override;

  bool needTransform() const {
    return _needTransform;
  }

  bool hasSmoothness() const {
    return _hasSmoothness;
  }

  const Matrix& deviceToLocal() const {
    return _deviceToLocal;
  }

  Rect localRect = {};
  // Per-corner radii in [TL, TR, BR, BL] order, scaled to device-equivalent units when
  // _needTransform is true (CPU side bakes axisScales into the radii).
  std::array<Point, 4> radii = {};
  // Corner smoothness in [0, 1], shared by all four corners.
  float smoothness = 0.0f;
  bool antiAlias = true;

 private:
  Matrix _deviceToLocal = Matrix::I();
  bool _needTransform = false;
  bool _hasSmoothness = false;
};

}  // namespace tgfx

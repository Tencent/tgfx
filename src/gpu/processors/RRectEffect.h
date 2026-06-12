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
 * Coverage FragmentProcessor for an axis-aligned or affine-mapped rounded rectangle clip.
 * Accepts RRects of type Oval / Simple / Complex; Rect type should be routed to RectEffect by
 * the caller. The shader uses an arc-box dispatch + half-pixel safeRadii guard so degenerate
 * inputs (sharp corners, sub-pixel radii, diagonally overlapping arc boxes) are handled
 * uniformly without CPU-side fallback.
 */
class RRectEffect : public FragmentProcessor {
 public:
  /**
   * @param rRect       Rounded rectangle in local space; type must not be Rect.
   * @param matrix      Maps the local-space rRect to device space. Defaults to identity.
   * @param antiAlias   If true, the shader produces a half-pixel AA edge; otherwise a hard edge.
   *
   * Returns nullptr when the matrix has perspective or its scale components are zero.
   */
  static PlacementPtr<RRectEffect> Make(BlockAllocator* allocator, const RRect& rRect,
                                        const Matrix& matrix = Matrix::I(), bool antiAlias = true);

  std::string name() const override {
    return "RRectEffect";
  }

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  RRectEffect(const Rect& localRect, const std::array<Point, 4>& radii, const Matrix& deviceToLocal,
              bool needTransform, bool antiAlias)
      : FragmentProcessor(ClassID()), localRect(localRect), radii(radii),
        deviceToLocal(deviceToLocal), needTransform(needTransform), antiAlias(antiAlias) {
  }

  void onComputeProcessorKey(BytesKey* bytesKey) const override;

  Rect localRect = {};
  // Per-corner radii in [TL, TR, BR, BL] order, scaled to device-equivalent units when
  // needTransform is true (CPU side bakes axisScales into the radii).
  std::array<Point, 4> radii = {};
  Matrix deviceToLocal = Matrix::I();
  bool needTransform = false;
  bool antiAlias = true;
};

}  // namespace tgfx

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
#include "tgfx/core/Matrix.h"

namespace tgfx {

/**
 * Coverage FragmentProcessor for an arbitrary axis-aligned or affine-mapped rectangle clip.
 * Supports both AA (smooth half-pixel edge) and NonAA (hard edge) operation through a uniform,
 * sharing the same program. The matrix may be identity (fast path: shader works directly in
 * device space) or any affine map (transform path: shader inverse-maps gl_FragCoord to local
 * space). Perspective and degenerate matrices are rejected; callers should fall back to a mask.
 */
class RectEffect : public FragmentProcessor {
 public:
  /**
   * @param localRect    The rectangle in local space.
   * @param matrix       Maps localRect to device space. Defaults to identity.
   * @param antiAlias    If true, the shader produces a half-pixel AA edge; otherwise a hard edge.
   *
   * Returns nullptr when matrix has perspective or its scale components are zero.
   */
  static PlacementPtr<RectEffect> Make(BlockAllocator* allocator, const Rect& localRect,
                                       const Matrix& matrix = Matrix::I(), bool antiAlias = true);

  std::string name() const override {
    return "RectEffect";
  }

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  RectEffect(const Rect& localRect, const Matrix& deviceToLocal, bool needTransform, bool antiAlias)
      : FragmentProcessor(ClassID()), localRect(localRect), deviceToLocal(deviceToLocal),
        needTransform(needTransform), antiAlias(antiAlias) {
  }

  void onComputeProcessorKey(BytesKey* bytesKey) const override;

  Rect localRect = {};
  // Identity when needTransform is false. Inverse of the (axisScales-stripped) device-to-local
  // matrix otherwise.
  Matrix deviceToLocal = Matrix::I();
  // Selects the shader variant; encoded into the ProcessorKey.
  bool needTransform = false;
  // AA flag: passed via uniform, does not affect ProcessorKey.
  bool antiAlias = true;
};

}  // namespace tgfx

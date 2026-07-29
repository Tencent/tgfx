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
 * Coverage FragmentProcessor for an affine-mapped rectangle clip, in either AA or NonAA mode.
 * Perspective and degenerate matrices are not supported.
 */
class RectEffect : public FragmentProcessor {
 public:
  /**
   * @param localRect    The rectangle in local space.
   * @param matrix       Maps localRect to device space. Defaults to identity.
   * @param antiAlias    If true, the shader produces a soft edge that ramps over one pixel centered
   *                     on the boundary; otherwise a hard edge.
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

  RectEffect(const Rect& localRect, const Matrix& deviceToLocal, bool antiAlias)
      : FragmentProcessor(ClassID()), localRect(localRect), antiAlias(antiAlias),
        _deviceToLocal(deviceToLocal), _needTransform(!deviceToLocal.isIdentity()) {
  }

  void onComputeProcessorKey(BytesKey* bytesKey) const override;

  bool needTransform() const {
    return _needTransform;
  }

  const Matrix& deviceToLocal() const {
    return _deviceToLocal;
  }

  Rect localRect = {};
  bool antiAlias = true;

 private:
  // The inverse of the axisScales-stripped device-to-local matrix, or identity when there is no
  // transform.
  Matrix _deviceToLocal = Matrix::I();
  // Whether a transform is needed, derived from whether _deviceToLocal is identity.
  bool _needTransform = false;
};

}  // namespace tgfx

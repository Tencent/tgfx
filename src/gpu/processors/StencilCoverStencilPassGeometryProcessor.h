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

#include "GeometryProcessor.h"
#include "tgfx/core/Matrix.h"

namespace tgfx {
/**
 * StencilCoverStencilPassGeometryProcessor drives the stencil pass of the stencil-and-cover
 * render path. The vertex stream feeds per-vertex position together with the Loop-Blinn KLM
 * coefficients (see StencilCoverPathTessellator.h for the algorithm reference). The fragment
 * shader evaluates the quadratic implicit-curve test `k*k - l > 0` and discards every pixel
 * sitting on the exterior side of the curve, so that the configured stencil op (typically
 * Invert under even-odd fill) toggles only the right pixels.
 *
 * Color output is intentionally a placeholder — the pipeline that owns this processor must
 * disable color writes (colorWriteMask = 0) so the stencil pass leaves the color attachment
 * untouched. The cover pass (StencilCoverCoverPassGeometryProcessor) re-shades the touched
 * pixels.
 */
class StencilCoverStencilPassGeometryProcessor : public GeometryProcessor {
 public:
  static PlacementPtr<StencilCoverStencilPassGeometryProcessor> Make(BlockAllocator* allocator,
                                                                     const Matrix& viewMatrix);

  std::string name() const override {
    return "StencilCoverStencilPassGeometryProcessor";
  }

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  explicit StencilCoverStencilPassGeometryProcessor(const Matrix& viewMatrix);

  // Attribute order must match the byte layout of the vertex stream produced by
  // StencilCoverPathTessellator. `position` and `klm` are read as a contiguous Attribute[2] by the
  // constructor's setVertexAttributes(&position, 2); a DEBUG_ASSERT in the .cpp file verifies
  // the two members really do sit at adjacent offsets so a future Attribute layout change is
  // caught at runtime instead of producing silently corrupt vertex bindings.
  Attribute position;
  Attribute klm;

  Matrix viewMatrix = {};
};
}  // namespace tgfx

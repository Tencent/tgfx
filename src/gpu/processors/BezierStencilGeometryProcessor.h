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
 * BezierStencilGeometryProcessor drives the stencil pass of the bezier rasterization render
 * path. The vertex stream feeds per-vertex position together with the implicit-curve
 * coefficients used to evaluate inside/outside in the fragment shader. The fragment shader is
 * responsible for discarding pixels outside the curve so that the configured stencil op
 * (typically Invert under even-odd fill) toggles only the right pixels.
 *
 * Color output is intentionally a placeholder — the pipeline that owns this processor must
 * disable color writes (colorWriteMask = 0) so the stencil pass leaves the color attachment
 * untouched. The cover pass (BezierCoverGeometryProcessor) re-shades the touched pixels.
 */
class BezierStencilGeometryProcessor : public GeometryProcessor {
 public:
  static PlacementPtr<BezierStencilGeometryProcessor> Make(BlockAllocator* allocator,
                                                           const Matrix& viewMatrix);

  std::string name() const override {
    return "BezierStencilGeometryProcessor";
  }

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  explicit BezierStencilGeometryProcessor(const Matrix& viewMatrix);

  // Attribute order must match the byte layout of the vertex stream produced by
  // ShapeBezierRasterizer. Keep `position` and `klm` adjacent — the constructor relies on
  // `setVertexAttributes(&position, 2)` walking the two consecutive members as an array.
  Attribute position;
  Attribute klm;

  Matrix viewMatrix = {};
};
}  // namespace tgfx

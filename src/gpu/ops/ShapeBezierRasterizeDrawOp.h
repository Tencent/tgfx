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

#include "DrawOp.h"
#include "gpu/processors/BezierStencilGeometryProcessor.h"
#include "gpu/proxies/ShapeBezierRasterizeGeometryProxy.h"
#include "gpu/proxies/VertexBufferView.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/PathTypes.h"
#include "tgfx/core/Rect.h"
#include "tgfx/gpu/RenderPipeline.h"

namespace tgfx {
/**
 * ShapeBezierRasterizeDrawOp draws a non-antialiased shape through the bezier rasterization
 * render path. It is the first stencil-based renderer in tgfx and runs two passes inside the
 * standard DrawOp::execute() flow:
 *
 *   1. Stencil pass — uses BezierStencilGeometryProcessor to walk the implicit-bezier vertex
 *      stream produced by ShapeBezierRasterizer; the configured stencil op (Invert for
 *      even-odd, IncrementWrap/DecrementWrap for non-zero) toggles the stencil buffer for the
 *      pixels that survive the fragment's curve test.
 *
 *   2. Cover pass — re-shades the surviving pixels under a device-bounds quad through the
 *      brush fragment-processor chain attached by OpsCompositor::addDrawOp.
 *
 * Both passes share the same RenderPass; the depth/stencil texture is attached upstream by
 * OpsRenderTask when needsStencil() returns true.
 */
class ShapeBezierRasterizeDrawOp : public DrawOp {
 public:
  static PlacementPtr<ShapeBezierRasterizeDrawOp> Make(
      std::shared_ptr<ShapeBezierRasterizeGeometryProxy> geometryProxy, PMColor color,
      const Matrix& viewMatrix, const Matrix& uvMatrix, const Rect& coverLocalBounds,
      PathFillType fillType);

  bool needsStencil() const override {
    return true;
  }

 protected:
  PlacementPtr<GeometryProcessor> onMakeGeometryProcessor(RenderTarget* renderTarget) override;

  void onConfigureProgramInfo(ProgramInfo& programInfo) override;

  void onDraw(RenderPass* renderPass) override;

  Type type() override {
    return Type::ShapeBezierRasterizeDrawOp;
  }

 private:
  std::shared_ptr<ShapeBezierRasterizeGeometryProxy> geometryProxy = nullptr;
  std::shared_ptr<VertexBufferView> coverQuadBuffer = nullptr;
  PMColor color = PMColor::Transparent();
  Matrix viewMatrix = {};
  Matrix uvMatrix = {};
  // The local-space rect that bounds the cover-pass quad. The cover GP applies the view
  // matrix the same way the stencil pass does, so this rect must be in the path's local
  // coordinate system (not device space).
  Rect coverLocalBounds = {};
  PathFillType fillType = PathFillType::Winding;

  // Stencil-pass GP. Made once at construction time so onDraw() can reuse it instead of
  // rebuilding the GP on every command-buffer encode pass.
  PlacementPtr<BezierStencilGeometryProcessor> stencilGP = nullptr;

  // Pre-computed depth/stencil descriptors. Both depend only on `fillType`, so caching them
  // avoids running the small but per-op MakeStencilPassDS / MakeCoverPassDS branches each
  // time onDraw() encodes the stencil and cover passes.
  DepthStencilDescriptor stencilPassDS = {};
  DepthStencilDescriptor coverPassDS = {};

  // Pre-computed cover-pass stencil reference value (depends only on fillType).
  uint32_t coverStencilRef = 0;

  ShapeBezierRasterizeDrawOp(BlockAllocator* allocator,
                             std::shared_ptr<ShapeBezierRasterizeGeometryProxy> geometryProxy,
                             PMColor color, const Matrix& viewMatrix, const Matrix& uvMatrix,
                             const Rect& coverLocalBounds, PathFillType fillType);

  friend class BlockAllocator;
};
}  // namespace tgfx

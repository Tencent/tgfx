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
#include "gpu/processors/StencilCoverStencilPassGeometryProcessor.h"
#include "gpu/proxies/StencilCoverPathProxy.h"
#include "gpu/proxies/VertexBufferView.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/PathTypes.h"
#include "tgfx/core/Rect.h"
#include "tgfx/gpu/RenderPipeline.h"

namespace tgfx {
/**
 * StencilCoverPathDrawOp draws a non-antialiased shape through tgfx's first stencil-based
 * renderer. The algorithm is a Loop-Blinn quadratic fill (see Loop & Blinn, SIGGRAPH 2005,
 * referenced from StencilCoverPathTessellator.h) layered on top of an NV_path_rendering-style
 * stencil-and-cover pipeline. Path geometry is not triangulated on the CPU; instead, every
 * bezier segment becomes a few triangles carrying KLM coefficients, and the fragment shader
 * uses the implicit-curve test `k*k - l > 0 ⇒ discard` to obtain pixel-accurate coverage on
 * the GPU.
 *
 * Two passes are run inside the standard DrawOp::execute() flow:
 *
 *   1. Stencil pass — uses StencilCoverStencilPassGeometryProcessor to walk the Loop-Blinn
 *      vertex stream produced by StencilCoverPathTessellator. The fragment shader runs the
 *      Loop-Blinn test and discards every pixel outside the curve; the configured stencil
 *      op (Invert for even-odd, IncrementWrap/DecrementWrap for non-zero) then toggles the
 *      stencil buffer for the surviving pixels, accumulating the fill-rule count.
 *
 *   2. Cover pass — re-shades the surviving pixels under a device-bounds quad through the
 *      brush fragment-processor chain attached by OpsCompositor::addDrawOp. The Loop-Blinn
 *      test does not run here — coverage is already locked into the stencil buffer.
 *
 * Both passes share the same RenderPass; the depth/stencil texture is attached upstream by
 * OpsRenderTask when needsStencil() returns true.
 */
class StencilCoverPathDrawOp : public DrawOp {
 public:
  static PlacementPtr<StencilCoverPathDrawOp> Make(
      std::shared_ptr<StencilCoverPathProxy> geometryProxy, PMColor color, const Matrix& viewMatrix,
      const Matrix& uvMatrix, const Rect& coverLocalBounds, PathFillType fillType);

  bool needsStencil() const override {
    return true;
  }

 protected:
  PlacementPtr<GeometryProcessor> onMakeGeometryProcessor(RenderTarget* renderTarget) override;

  void onConfigureProgramInfo(ProgramInfo& programInfo) override;

  void onDraw(RenderPass* renderPass, RenderTarget* renderTarget) override;

  Type type() override {
    return Type::StencilCoverPathDrawOp;
  }

 private:
  std::shared_ptr<StencilCoverPathProxy> geometryProxy = nullptr;
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
  PlacementPtr<StencilCoverStencilPassGeometryProcessor> stencilGP = nullptr;

  // Pre-computed depth/stencil descriptors. Both depend only on `fillType`, so caching them
  // avoids running the small but per-op MakeStencilPassDS / MakeCoverPassDS branches each
  // time onDraw() encodes the stencil and cover passes.
  DepthStencilDescriptor stencilPassDS = {};
  DepthStencilDescriptor coverPassDS = {};

  // Pre-computed cover-pass stencil reference value (depends only on fillType).
  uint32_t coverStencilRef = 0;

  StencilCoverPathDrawOp(BlockAllocator* allocator,
                         std::shared_ptr<StencilCoverPathProxy> geometryProxy, PMColor color,
                         const Matrix& viewMatrix, const Matrix& uvMatrix,
                         const Rect& coverLocalBounds, PathFillType fillType);

  // Builds the stencil-pass ProgramInfo (stencil GP, no FP chain, no xfer processor, colour
  // writes disabled) and binds the resulting pipeline together with its uniforms and samplers
  // to the render pass. Counterpart of DrawOp::bindStandardPipeline() for the cover pass —
  // both helpers leave the render pass ready for vertex-buffer + draw calls. Returns false
  // if program creation fails, in which case the caller should abort the stencil pass.
  bool bindStencilPipeline(RenderPass* renderPass, RenderTarget* renderTarget);

  friend class BlockAllocator;
};
}  // namespace tgfx

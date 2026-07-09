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
#include "gpu/ProgramInfo.h"

namespace tgfx {
/**
 * StandardDrawOp is the base class for ops that follow the "framework binds one pipeline,
 * subclass issues one draw call" pattern. execute() is final: it builds the op's ProgramInfo
 * from the geometry processor supplied by onMakeGeometryProcessor() and the color/coverage
 * fragment processors accumulated on DrawOp, gives subclasses a chance to tweak the resulting
 * ProgramInfo via onConfigureProgramInfo(), then binds the pipeline (together with uniforms,
 * samplers and scissor) before delegating the actual draw call to onDraw().
 *
 * Ops that need to run multiple pipelines within a single render pass (for example
 * StencilCoverPathDrawOp, which runs a stencil pass followed by a cover pass) must not derive
 * from this class — they should inherit from DrawOp directly and take full responsibility for
 * pipeline binding inside their own execute() implementation.
 */
class StandardDrawOp : public DrawOp {
 public:
  void execute(RenderPass* renderPass, RenderTarget* renderTarget) final;

 protected:
  StandardDrawOp(BlockAllocator* allocator, AAType aaType) : DrawOp(allocator, aaType) {
  }

  /**
   * Builds the geometry processor that drives this op's pipeline. Called once per execute().
   */
  virtual PlacementPtr<GeometryProcessor> onMakeGeometryProcessor(RenderTarget* renderTarget) = 0;

  /**
   * Hook invoked right before the ProgramInfo is materialised, giving the op a chance to
   * inject pipeline-level overrides such as the depth/stencil descriptor or the colour write
   * mask. The default does nothing; only ops that need non-default pipeline state override it.
   */
  virtual void onConfigureProgramInfo(ProgramInfo& /*programInfo*/) {
  }

  /**
   * Issues draw calls for this op. Invoked after the standard pipeline has been bound to the
   * render pass together with its uniforms, samplers and scissor rectangle. Subclasses only
   * need to bind buffers and call renderPass->draw()/drawIndexed().
   */
  virtual void onDraw(RenderPass* renderPass, RenderTarget* renderTarget) = 0;

 private:
  /**
   * Assembles the op's ProgramInfo, materialises the pipeline, and binds it to the render
   * pass together with uniforms, samplers and the scissor rectangle. Returns false if program
   * creation fails, in which case execute() aborts before calling onDraw().
   */
  bool bindStandardPipeline(RenderPass* renderPass, RenderTarget* renderTarget);
};
}  // namespace tgfx

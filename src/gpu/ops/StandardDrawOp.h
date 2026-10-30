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
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "DrawOp.h"
#include "gpu/ProgramInfo.h"

namespace tgfx {
/**
 * StandardDrawOp is the base class for ops that follow the "framework binds one pipeline,
 * subclass issues one draw call" pattern. execute() is final: it first gives the subclass a
 * chance to veto the whole execution via onPrepare() when runtime inputs turn out to be
 * unusable, then builds the op's ProgramInfo from the geometry processor supplied by
 * onMakeGeometryProcessor() and the color/coverage fragment processors accumulated on DrawOp
 * (probing whether the AOT route's fused kernels serve the unmodified draw through the plain
 * program lookup), gives subclasses a chance to tweak the resulting ProgramInfo via
 * onConfigureProgramInfo(), then binds the pipeline (together with uniforms, samplers and
 * scissor) before delegating the actual draw call to onDraw().
 *
 * Ops that need to run multiple pipelines within a single render pass (for example
 * StencilCoverPathDrawOp, which runs a stencil pass followed by a cover pass) must not derive
 * from this class — they should inherit from DrawOp directly and take full responsibility for
 * pipeline binding inside their own execute() implementation.
 */
class StandardDrawOp : public DrawOp {
 public:
  void execute(RenderPass* renderPass, RenderTarget* renderTarget) final;

  /**
   * Prepares the geometry processor and program for a draw. When colorOverride is present, its
   * processors replace the op's color processors without modifying them.
   */
  bool prepare(RenderTarget* renderTarget,
               ProgramLookupMode mode = ProgramLookupMode::AllowRuntimeFallback,
               std::optional<ColorProcessorList> colorOverride = std::nullopt,
               PixelFormat depthStencilFormat = PixelFormat::Unknown);

  /**
   * Executes a successfully prepared draw and uploads its uniforms and texture samplers. Set
   * recordDrawStats to false when a caller aggregates multiple prepared passes into one logical
   * Draw-level record. Returns false when nothing was submitted (the draw is not prepared, or its
   * uniforms have no backing store), so such callers do not count the pass as executed.
   */
  bool executePrepared(RenderPass* renderPass, RenderTarget* renderTarget,
                       bool recordDrawStats = true);

  /**
   * The task-driven equivalent of execute()'s prepare half: runs onPrepare() FIRST and then
   * prepare(). Callers that drive prepare()/executePrepared() directly (the AOT plan task,
   * whose passes render through its own render-pass sequence) must use this entry point
   * instead of prepare(): prepare() alone skips the onPrepare() lifecycle hook, and ops that
   * validate asynchronously produced buffers there (hairline ops with possibly-empty vertex
   * segments) would reach onDraw() with null buffers — execute() runs the hook, so a
   * task-driven op must not silently lose it.
   */
  bool prepareForTask(RenderTarget* renderTarget, ProgramLookupMode mode,
                      std::optional<ColorProcessorList> colorOverride = std::nullopt);

  /**
   * The geometry color every fragment of this op receives as its input color, or nullopt when
   * the color varies per vertex or the op does not report it. The AOT route uses it to decide
   * whether intermediate fills, which run with an opaque white input, see the same input.
   */
  virtual std::optional<PMColor> uniformGeometryColor() const {
    return std::nullopt;
  }

  /**
   * The matrix mapping this op's top-left device positions onto the local coordinates its
   * fragment processors evaluate at, when that mapping is one affine matrix for the whole op, or
   * nullopt otherwise (per-vertex UV coordinates, or an op that does not report it).
   */
  virtual std::optional<Matrix> deviceToLocalMatrix() const {
    return std::nullopt;
  }

 protected:
  StandardDrawOp(BlockAllocator* allocator, AAType aaType) : DrawOp(allocator, aaType) {
  }

  /**
   * Optional hook invoked at the start of execute(), before any pipeline binding takes place.
   * Return false to skip this op entirely — no geometry processor, pipeline binding, uniform
   * upload, sampler binding or draw call will be issued. This is the right place to reject an
   * op whose runtime inputs (e.g. an asynchronously produced vertex buffer) turned out to be
   * empty, so it does not leave dirty pipeline state behind for the next op. The default
   * returns true.
   *
   * Contract with onDraw(): any resource this method validates as non-null MAY be used
   * unconditionally by the subsequent onDraw() call without re-checking. Both hooks run
   * synchronously on the same thread inside a single execute() invocation, so a resource
   * cannot transition from ready to null between them. Subclasses that add null-checks here
   * are therefore free to omit the corresponding checks in onDraw().
   */
  virtual bool onPrepare() {
    return true;
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
  bool interpreterServesDraw(RenderTarget* renderTarget);

  PlacementPtr<GeometryProcessor> geometryProcessor = nullptr;
  bool geometryProcessorInitialized = false;
  std::optional<ColorProcessorList> preparedColors = std::nullopt;
  std::unique_ptr<ProgramInfo> preparedProgramInfo = nullptr;
  std::shared_ptr<Program> preparedProgram = nullptr;
  RenderTarget* preparedRenderTarget = nullptr;
  bool preparedViaDecomposition = false;
};
}  // namespace tgfx

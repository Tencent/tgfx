/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include <optional>
#include "gpu/AAType.h"
#include "gpu/aot/bundle/PrecompiledShaderCache.h"
#include "gpu/aot/ir/DrawIR.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/processors/XferProcessor.h"
#include "gpu/resources/RenderTarget.h"
#include "tgfx/gpu/RenderPass.h"

namespace tgfx {

class AOTRouter;

/**
 * DrawOp is the minimal contract every deferred draw operation must satisfy. It exposes only
 * what OpsRenderTask needs to schedule an op inside a render pass: an execute() entry point.
 * Concrete pipeline-binding strategies live in subclasses — StandardDrawOp handles the common
 * "framework binds a single pipeline, subclass issues one draw call" pattern; ops that manage
 * their own pipelines inherit from DrawOp directly and implement execute() themselves.
 */
class DrawOp {
 public:
  using ColorProcessorList = std::vector<PlacementPtr<FragmentProcessor>>;

  enum class Type {
    RectDrawOp,
    RRectDrawOp,
    ShapeDrawOp,
    AtlasTextOp,
    Quads3DDrawOp,
    MeshDrawOp,
    HairlineLineOp,
    HairlineQuadOp,
    ShapeInstancedDrawOp,
  };

  virtual ~DrawOp();

  void setScissorRect(const Rect& rect) {
    scissorRect = rect;
  }

  void setBlendMode(BlendMode mode) {
    blendMode = mode;
  }

  void setCullMode(CullMode mode) {
    cullMode = mode;
  }

  void setXferProcessor(PlacementPtr<XferProcessor> processor) {
    xferProcessor = std::move(processor);
  }

  void addColorFP(PlacementPtr<FragmentProcessor> colorProcessor) {
    colors.emplace_back(std::move(colorProcessor));
  }

  void addCoverageFP(PlacementPtr<FragmentProcessor> coverageProcessor) {
    coverages.emplace_back(std::move(coverageProcessor));
  }

  std::vector<PlacementPtr<FragmentProcessor>>& colorProcessors() {
    return colors;
  }

  std::vector<PlacementPtr<FragmentProcessor>>& coverageProcessors() {
    return coverages;
  }

  /**
   * The draw's whole expression lowered once at record time: the color graph plus the classified
   * coverage terms, with the xfer processor and blend mode riding along. Null when the expression
   * did not lower — the plain FP path serves the draw unchanged. Nothing consumes the IR until
   * the prepare-time AOT branch reads it.
   */
  const DrawIR* loweredIR() const {
    return ir.get();
  }

  /**
   * Appends the coverage processors added after the record-time lowering — a rasterized shape
   * attaches its mask coverage only once the shape proxy exists — as new IR terms, so the carried
   * IR stays the draw's complete expression. Returns true when the IR now covers the live list;
   * false when the grown expression does not lower, which leaves the IR unusable for this draw.
   */
  bool appendGrownCoverage();

  /**
   * The record-time source of the AtlasText glyph leaf: the op's atlas proxy and sampling, which
   * the prepare-time geometry processor merely re-derives. Nullopt for every other op. Both atlas
   * flavors lower: the glyph leaf's parameters carry the atlas's alpha-only fact, the chain
   * refuses a color glyph, and the dedicated atlas kernel carries it.
   */
  virtual std::optional<AOTAtlasGlyphSource> recordTimeAtlasSource() const {
    return std::nullopt;
  }

  /**
   * Adopts a plan pass's IR: the op's expression is that pass (a cut segment built by the plan
   * executor), so the fill draw carries IR instead of a processor and the prepare-time serving
   * rebuilds the processor once. The override replaces any record-time IR and the op's own
   * processors are ignored while it is set.
   */
  void adoptLoweredIR(PlacementPtr<DrawIR> passIR);

  /**
   * Whether the draw produces fractional coverage from any source: its coverage processors, or its
   * geometry processor (analytic AA edges, glyph masks, per-vertex coverage). Decides whether the
   * blend needs a dst texture.
   */
  virtual bool hasCoverage() const {
    return !coverages.empty();
  }

  /**
   * Whether the draw carries coverage processors (clip or mask coverage FPs). Coverage produced by
   * the geometry processor does not count: it stays inside the geometry processor whatever color
   * processors the AOT route puts on the draw.
   */
  bool hasCoverageProcessors() const {
    return !coverages.empty();
  }

  /**
   * Encodes the op into the given render pass. Called by OpsRenderTask after the pass has
   * been begun with the correct attachments. Concrete subclasses decide how the op reaches
   * the GPU — StandardDrawOp does one bindPipeline + one draw call, while custom multi-pass
   * ops build their own pipelines here.
   */
  virtual void execute(RenderPass* renderPass, RenderTarget* renderTarget) = 0;

  virtual Type type() const = 0;

 protected:
  BlockAllocator* allocator = nullptr;
  AAType aaType = AAType::None;
  Rect scissorRect = {};
  std::vector<PlacementPtr<FragmentProcessor>> colors = {};
  std::vector<PlacementPtr<FragmentProcessor>> coverages = {};
  PlacementPtr<XferProcessor> xferProcessor = nullptr;
  PlacementPtr<DrawIR> ir = nullptr;
  BlendMode blendMode = BlendMode::SrcOver;
  CullMode cullMode = CullMode::None;

  DrawOp(BlockAllocator* allocator, AAType aaType);

  /**
   * Applies the op's scissor rectangle to the render pass. An empty scissor expands to the
   * full render target. StandardDrawOp calls this internally as part of pipeline binding;
   * multi-pass ops that bind their own pipelines must call it (or a stricter variant) for
   * every pass — otherwise that pass writes outside the op's clip and may pollute the render
   * pass for subsequent ops (especially relevant for stencil writes, which are not undone by
   * a cover pass outside the cover scissor).
   */
  void applyScissor(RenderPass* renderPass, RenderTarget* renderTarget) const;

 private:
  // The record-time route's private levers (AOTRouter): the lowering and the in-place pass
  // annotation belong to that route alone, so nothing else drives them out of order.
  friend class AOTRouter;

  /**
   * Lowers the op's colors, coverages, xfer processor and blend mode into a DrawIR placed on the
   * drawing allocator. Call once at record time, after those members are set and only while the
   * precompiled route is active. A lowering failure leaves the IR null — the draw keeps the plain
   * path, which is exactly the pre-IR behavior.
   */
  void tryLowerIR();
};
}  // namespace tgfx

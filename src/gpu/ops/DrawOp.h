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
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "gpu/AAType.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/processors/XferProcessor.h"
#include "gpu/resources/RenderTarget.h"
#include "tgfx/gpu/RenderPass.h"

namespace tgfx {
/**
 * DrawOp is the minimal contract every deferred draw operation must satisfy. It exposes only
 * what OpsRenderTask needs to schedule an op inside a render pass: an execute() entry point,
 * plus flags such as needsStencil() that let the task set up the pass correctly. Concrete
 * pipeline-binding strategies live in subclasses — StandardDrawOp handles the common
 * "framework binds a single pipeline, subclass issues one draw call" pattern; ops that manage
 * their own pipelines (e.g. StencilCoverPathDrawOp with its stencil + cover passes) inherit
 * from DrawOp directly and implement execute() themselves.
 */
class DrawOp {
 public:
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
    StencilCoverPathDrawOp,
  };

  virtual ~DrawOp() = default;

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

  virtual bool hasCoverage() const {
    return !coverages.empty();
  }

  /**
   * Returns true when the op needs a depth/stencil attachment to be present on the render
   * pass. OpsRenderTask scans the op list with this hook before beginning the pass and attaches
   * a stencil texture when at least one op opts in. Default is false so existing draw ops keep
   * running without a stencil buffer.
   */
  virtual bool needsStencil() const {
    return false;
  }

  /**
   * Encodes the op into the given render pass. Called by OpsRenderTask after the pass has
   * been begun with the correct attachments. Concrete subclasses decide how the op reaches
   * the GPU — StandardDrawOp does one bindPipeline + one draw call, while custom multi-pass
   * ops build their own pipelines here.
   */
  virtual void execute(RenderPass* renderPass, RenderTarget* renderTarget) = 0;

 protected:
  BlockAllocator* allocator = nullptr;
  AAType aaType = AAType::None;
  Rect scissorRect = {};
  std::vector<PlacementPtr<FragmentProcessor>> colors = {};
  std::vector<PlacementPtr<FragmentProcessor>> coverages = {};
  PlacementPtr<XferProcessor> xferProcessor = nullptr;
  BlendMode blendMode = BlendMode::SrcOver;
  CullMode cullMode = CullMode::None;

  DrawOp(BlockAllocator* allocator, AAType aaType) : allocator(allocator), aaType(aaType) {
  }

  /**
   * Applies the op's scissor rectangle to the render pass. An empty scissor expands to the
   * full render target. StandardDrawOp calls this internally as part of pipeline binding;
   * multi-pass ops that bind their own pipelines must call it (or a stricter variant) for
   * every pass — otherwise that pass writes outside the op's clip and may pollute the render
   * pass for subsequent ops (especially relevant for stencil writes, which are not undone by
   * a cover pass outside the cover scissor).
   */
  void applyScissor(RenderPass* renderPass, RenderTarget* renderTarget) const;

  virtual Type type() = 0;
};
}  // namespace tgfx

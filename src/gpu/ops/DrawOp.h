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
#include "gpu/ProgramInfo.h"
#include "tgfx/gpu/RenderPass.h"

namespace tgfx {
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

  void execute(RenderPass* renderPass, RenderTarget* renderTarget);

  /**
   * Returns true when the op needs a depth/stencil attachment to be present on the render
   * pass. OpsRenderTask scans the op list with this hook before beginning the pass and attaches
   * a stencil texture when at least one op opts in. Default is false so existing draw ops keep
   * running without a stencil buffer.
   */
  virtual bool needsStencil() const {
    return false;
  }

 protected:
  BlockAllocator* allocator = nullptr;
  AAType aaType = AAType::None;
  Rect scissorRect = {};
  std::vector<PlacementPtr<FragmentProcessor>> colors = {};
  std::vector<PlacementPtr<FragmentProcessor>> coverages = {};
  PlacementPtr<XferProcessor> xferProcessor = nullptr;
  BlendMode blendMode = BlendMode::SrcOver;
  CullMode cullMode = CullMode::None;

  /**
   * Set to the current render target by execute() for the duration of the onDraw() call, and
   * reset to nullptr afterwards. Ops that need to build their own ProgramInfo from inside
   * onDraw() — e.g. multi-pass ops that run a self-managed stencil pass — read it through
   * this member. For every other op onDraw() still works purely off the RenderPass argument
   * and can ignore this field.
   */
  RenderTarget* currentRenderTarget = nullptr;

  /**
   * Set by execute() to the Program and ProgramInfo it materialised for this op. Available
   * for the duration of the onDraw() call, then reset to nullptr. Subclasses that need to
   * re-bind the standard pipeline mid-draw — typically multi-pass ops whose stencil/cover
   * sub-passes replace the bound pipeline — can reuse these instead of building a duplicate
   * ProgramInfo + getProgram round-trip. For every other op these stay unread.
   *
   * Lifetime note: currentProgramInfo points at the ProgramInfo on execute()'s stack frame
   * and is only valid inside onDraw(). Do not stash it anywhere with a longer lifetime.
   */
  Program* currentProgram = nullptr;
  ProgramInfo* currentProgramInfo = nullptr;

  DrawOp(BlockAllocator* allocator, AAType aaType) : allocator(allocator), aaType(aaType) {
  }

  virtual PlacementPtr<GeometryProcessor> onMakeGeometryProcessor(RenderTarget* renderTarget) = 0;

  /**
   * Hook invoked from execute() right before the program is materialised, giving the op a chance
   * to inject pipeline-level overrides such as the depth/stencil descriptor or the colour write
   * mask into the ProgramInfo. The default does nothing; only ops that opt into stencil-based
   * rendering need to override.
   */
  virtual void onConfigureProgramInfo(ProgramInfo& /*programInfo*/) {
  }

  virtual void onDraw(RenderPass* renderPass) = 0;

  virtual Type type() = 0;
};
}  // namespace tgfx

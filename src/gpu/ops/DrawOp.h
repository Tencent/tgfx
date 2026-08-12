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

#include <optional>
#include "gpu/AAType.h"
#include "gpu/PrecompiledShaderCache.h"
#include "gpu/ProgramInfo.h"
#include "tgfx/gpu/RenderPass.h"

namespace tgfx {
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

  void setOffscreenFillDiagnostic(OffscreenFillKey key) {
    offscreenFillKey = key;
  }

  /**
   * Moves all coverage processors to the end of the color processor list. Used when a coverage
   * effect is fused into the color chain (e.g., a local texture mask folded into a pointwise
   * chain), which is only valid for blends that do not need a dst texture.
   */
  void moveCoveragesToColors() {
    for (auto& coverage : coverages) {
      colors.emplace_back(std::move(coverage));
    }
    coverages.clear();
  }

  std::vector<PlacementPtr<FragmentProcessor>>& colorProcessors() {
    return colors;
  }

  size_t numColorProcessors() const {
    return colors.size();
  }

  bool hasXferProcessor() const {
    return xferProcessor != nullptr;
  }

  virtual bool hasCoverage() const {
    return !coverages.empty();
  }

  /**
   * Prepares the geometry processor and program for a draw. When colorOverride is present, its
   * processors replace the DrawOp's color processors without modifying them.
   */
  bool prepare(RenderTarget* renderTarget,
               ProgramLookupMode mode = ProgramLookupMode::AllowRuntimeFallback,
               std::optional<ColorProcessorList> colorOverride = std::nullopt);

  /**
   * Executes a successfully prepared draw and uploads its uniforms and texture samplers. Set
   * recordDrawStats to false when a caller aggregates multiple prepared passes into one logical
   * Draw-level record.
   */
  void executePrepared(RenderPass* renderPass, bool recordDrawStats = true);

  void execute(RenderPass* renderPass, RenderTarget* renderTarget);

  virtual Type type() const = 0;

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

  virtual PlacementPtr<GeometryProcessor> onMakeGeometryProcessor(RenderTarget* renderTarget) = 0;

  /**
   * Provides a chain-route geometry processor for the decomposition rewrite, plus any coverage
   * leaves synthesized from GP-owned textures. AtlasTextOp implements this because its GP owns
   * the atlas sampler, which ProgramInfo would bind ahead of the chain's leaf samplers; the
   * returned GP is identical except it claims no sampler. The default returns nullptr, keeping
   * the draw's own geometry processor for the rewrite.
   */
  virtual PlacementPtr<GeometryProcessor> onMakeChainGeometryProcessor(
      std::vector<PlacementPtr<FragmentProcessor>>* chainCoverageFPs) {
    (void)chainCoverageFPs;
    return nullptr;
  }

  virtual void onDraw(RenderPass* renderPass) = 0;

 private:
  std::shared_ptr<Program> prepareDecomposedProgram(RenderTarget* renderTarget,
                                                    const ColorProcessorList& activeColors);

  PlacementPtr<GeometryProcessor> geometryProcessor = nullptr;
  // Holds the chain-route twin GP (when an op provides one) for the draw's lifetime: a local
  // PlacementPtr would run the base destructor at scope exit and devolve the vtable.
  PlacementPtr<GeometryProcessor> chainGeometryProcessor = nullptr;
  bool geometryProcessorInitialized = false;
  std::optional<ColorProcessorList> preparedColors = std::nullopt;
  std::unique_ptr<ProgramInfo> preparedProgramInfo = nullptr;
  std::shared_ptr<Program> preparedProgram = nullptr;
  RenderTarget* preparedRenderTarget = nullptr;
  OffscreenFillKey offscreenFillKey = InvalidOffscreenFillKey;
};
}  // namespace tgfx

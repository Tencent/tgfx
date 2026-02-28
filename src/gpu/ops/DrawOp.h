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
    HairlineLineOp,
    HairlineQuadOp,
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

  virtual void onDraw(RenderPass* renderPass) = 0;

  virtual Type type() = 0;
};
}  // namespace tgfx

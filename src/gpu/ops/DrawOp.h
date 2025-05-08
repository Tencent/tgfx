/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "Op.h"
#include "gpu/AAType.h"
#include "gpu/Pipeline.h"
#include "gpu/RenderPass.h"

namespace tgfx {
class DrawOp : public Op {
 public:
  PlacementPtr<Pipeline> createPipeline(RenderPass* renderPass, PlacementPtr<GeometryProcessor> gp);

  const Rect& scissorRect() const {
    return _scissorRect;
  }

  void setScissorRect(Rect scissorRect) {
    _scissorRect = scissorRect;
  }

  void setBlendFormula(const BlendFormula& formula) {
    _formula = formula;
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

  void setXferProcessor(PlacementPtr<XferProcessor> xferProcessor) {
    _xferProcessor = std::move(xferProcessor);
  }

 protected:
  AAType aaType = AAType::None;

  explicit DrawOp(AAType aaType) : aaType(aaType) {
  }

 private:
  Rect _scissorRect = {};
  std::vector<PlacementPtr<FragmentProcessor>> colors = {};
  std::vector<PlacementPtr<FragmentProcessor>> coverages = {};
  PlacementPtr<XferProcessor> _xferProcessor = nullptr;
  BlendFormula _formula = {};
};
}  // namespace tgfx

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

#include <functional>
#include "Op.h"
#include "gpu/AAType.h"
#include "gpu/Pipeline.h"
#include "gpu/RenderPass.h"
#include "tgfx/core/Image.h"

namespace tgfx {
class DrawOp : public Op {
 public:
  static std::unique_ptr<DrawOp> Make(std::shared_ptr<Image> image, const DrawArgs& args,
                                      const Matrix* localMatrix = nullptr,
                                      TileMode tileModeX = TileMode::Clamp,
                                      TileMode tileModeY = TileMode::Clamp);

  explicit DrawOp(uint8_t classID) : Op(classID) {
  }

  std::unique_ptr<Pipeline> createPipeline(RenderPass* renderPass,
                                           std::unique_ptr<GeometryProcessor> gp);

  const Rect& scissorRect() const {
    return _scissorRect;
  }

  void setScissorRect(Rect scissorRect) {
    _scissorRect = scissorRect;
  }

  void setBlendMode(BlendMode mode) {
    blendMode = mode;
  }

  void setAA(AAType type) {
    aa = type;
  }

  void addColorFP(std::unique_ptr<FragmentProcessor> colorProcessor) {
    _colors.emplace_back(std::move(colorProcessor));
  }

  void addMaskFP(std::unique_ptr<FragmentProcessor> maskProcessor) {
    _masks.emplace_back(std::move(maskProcessor));
  }

 protected:
  AAType aa = AAType::None;

  bool onCombineIfPossible(Op* op) override;

 private:
  Rect _scissorRect = Rect::MakeEmpty();
  std::vector<std::unique_ptr<FragmentProcessor>> _colors;
  std::vector<std::unique_ptr<FragmentProcessor>> _masks;
  BlendMode blendMode = BlendMode::SrcOver;
};
}  // namespace tgfx

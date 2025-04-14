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

#include "DrawOp.h"
#include "gpu/proxies/GpuShapeProxy.h"
#include "tgfx/core/Shape.h"

namespace tgfx {
class ShapeDrawOp : public DrawOp {
 public:
  static PlacementPtr<ShapeDrawOp> Make(std::shared_ptr<GpuShapeProxy> shapeProxy, Color color,
                                        const Matrix& uvMatrix, AAType aaType);

  void execute(RenderPass* renderPass) override;

 private:
  std::shared_ptr<GpuShapeProxy> shapeProxy = nullptr;
  Color color = Color::Transparent();
  Matrix uvMatrix = {};
  std::vector<float> maskVertices = {};

  ShapeDrawOp(std::shared_ptr<GpuShapeProxy> shapeProxy, Color color, const Matrix& uvMatrix,
              AAType aaType);

  friend class BlockBuffer;
};
}  // namespace tgfx

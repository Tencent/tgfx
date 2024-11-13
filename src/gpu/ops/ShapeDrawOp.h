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
  DEFINE_OP_CLASS_ID

  static std::unique_ptr<ShapeDrawOp> Make(Color color, std::shared_ptr<Shape> shape,
                                           const Matrix& viewMatrix);

  void prepare(Context* context, uint32_t renderFlags) override;

  void execute(RenderPass* renderPass) override;

 protected:
  bool onCombineIfPossible(Op* op) override;

 private:
  Color color = Color::Transparent();
  std::shared_ptr<Shape> shape = nullptr;
  Matrix viewMatrix = Matrix::I();
  Matrix uvMatrix = Matrix::I();
  Matrix rasterizeMatrix = Matrix::I();
  std::shared_ptr<GpuShapeProxy> shapeProxy = nullptr;

  ShapeDrawOp(Color color, std::shared_ptr<Shape> shape, const Matrix& viewMatrix,
              const Matrix& uvMatrix);
};
}  // namespace tgfx

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

#include "DrawOp.h"
#include "gpu/proxies/GPUShapeProxy.h"
#include "gpu/proxies/VertexBufferView.h"

namespace tgfx {
class ShapeDrawOp : public DrawOp {
 public:
  static PlacementPtr<ShapeDrawOp> Make(std::shared_ptr<GPUShapeProxy> shapeProxy, PMColor color,
                                        const Matrix& uvMatrix, AAType aaType);

  bool hasCoverage() const override;

 protected:
  PlacementPtr<GeometryProcessor> onMakeGeometryProcessor(RenderTarget* renderTarget) override;

  void onDraw(RenderPass* renderPass) override;

  Type type() override {
    return Type::ShapeDrawOp;
  }

 private:
  std::shared_ptr<GPUShapeProxy> shapeProxy = nullptr;
  std::shared_ptr<VertexBufferView> maskBufferProxy = {};
  PMColor color = Color::Transparent().premultiply();
  Matrix uvMatrix = {};

  ShapeDrawOp(std::shared_ptr<GPUShapeProxy> proxy, PMColor color, const Matrix& uvMatrix,
              AAType aaType);

  friend class BlockAllocator;
};
}  // namespace tgfx

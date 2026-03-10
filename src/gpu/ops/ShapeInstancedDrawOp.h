/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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
#include "tgfx/core/Color.h"
#include "tgfx/core/ColorSpace.h"

namespace tgfx {
class ShapeInstancedDrawOp : public DrawOp {
 public:
  static PlacementPtr<ShapeInstancedDrawOp> Make(std::shared_ptr<GPUShapeProxy> shapeProxy,
                                                 const Matrix* matrices, const Color* colors,
                                                 size_t count, PMColor gpColor,
                                                 const Matrix& uvMatrix, const Matrix& stateMatrix,
                                                 AAType aaType,
                                                 std::shared_ptr<ColorSpace> dstColorSpace);

  bool hasCoverage() const override;

 protected:
  PlacementPtr<GeometryProcessor> onMakeGeometryProcessor(RenderTarget* renderTarget) override;

  void onDraw(RenderPass* renderPass) override;

  Type type() override {
    return Type::ShapeInstancedDrawOp;
  }

 private:
  std::shared_ptr<GPUShapeProxy> shapeProxy = nullptr;
  std::shared_ptr<VertexBufferView> maskBufferProxy = nullptr;
  // Allocated by BlockAllocator, owned by the same drawing context. No need to free.
  const Matrix* matrices = nullptr;
  const Color* instanceColors = nullptr;
  size_t instanceCount = 0;
  PMColor gpColor = PMColor::Transparent();
  Matrix uvMatrix = {};
  Matrix stateMatrix = {};
  std::shared_ptr<ColorSpace> dstColorSpace = nullptr;

  ShapeInstancedDrawOp(BlockAllocator* allocator, std::shared_ptr<GPUShapeProxy> proxy,
                       const Matrix* matrices, const Color* instanceColors, size_t count,
                       PMColor gpColor, const Matrix& uvMatrix, const Matrix& stateMatrix,
                       AAType aaType, std::shared_ptr<ColorSpace> dstColorSpace);

  friend class BlockAllocator;
};
}  // namespace tgfx

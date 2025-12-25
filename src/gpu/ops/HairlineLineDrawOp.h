/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "gpu/ops/DrawOp.h"
#include "gpu/proxies/GPUBufferProxy.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/Matrix.h"

namespace tgfx {

class HairlineLineDrawOp final : public DrawOp {
 public:
  static PlacementPtr<HairlineLineDrawOp> Make(std::shared_ptr<GPUBufferProxy> lineVertexBuffer,
                                               std::shared_ptr<GPUBufferProxy> lineIndexBuffer,
                                               PMColor color, const Matrix& uvMatrix);

 protected:
  PlacementPtr<GeometryProcessor> onMakeGeometryProcessor(RenderTarget* renderTarget) override;

  void onDraw(RenderPass* renderPass) override;

  Type type() override {
    return Type::HairlineLineDrawOp;
  }

 private:
  HairlineLineDrawOp(BlockAllocator* allocator, std::shared_ptr<GPUBufferProxy> vertexBuffer,
                     std::shared_ptr<GPUBufferProxy> indexBuffer, PMColor color,
                     const Matrix& uvMatrix);

  std::shared_ptr<GPUBufferProxy> lineVertexBuffer;
  std::shared_ptr<GPUBufferProxy> lineIndexBuffer;
  PMColor color;
  Matrix uvMatrix;

  friend class BlockAllocator;
};

}  // namespace tgfx

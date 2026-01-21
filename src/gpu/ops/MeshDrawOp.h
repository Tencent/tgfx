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
#include "gpu/proxies/GPUMeshProxy.h"

namespace tgfx {

class MeshDrawOp : public DrawOp {
 public:
  static PlacementPtr<MeshDrawOp> Make(std::shared_ptr<GPUMeshProxy> meshProxy, PMColor color,
                                       const Matrix& viewMatrix, AAType aaType);

  bool hasCoverage() const override {
    return false;
  }

 protected:
  PlacementPtr<GeometryProcessor> onMakeGeometryProcessor(RenderTarget* renderTarget) override;

  void onDraw(RenderPass* renderPass) override;

  Type type() override {
    return Type::MeshDrawOp;
  }

 private:
  MeshDrawOp(BlockAllocator* allocator, std::shared_ptr<GPUMeshProxy> meshProxy, PMColor color,
             const Matrix& viewMatrix, AAType aaType);

  std::shared_ptr<GPUMeshProxy> meshProxy = nullptr;
  PMColor color = PMColor::Transparent();
  Matrix viewMatrix = {};

  friend class BlockAllocator;
};

}  // namespace tgfx

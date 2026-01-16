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
#include "gpu/proxies/GPUHairlineProxy.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/Matrix.h"

namespace tgfx {

class HairlineLineOp final : public DrawOp {
 public:
  static PlacementPtr<HairlineLineOp> Make(std::shared_ptr<GPUHairlineProxy> hairlineProxy,
                                           PMColor color, const Matrix& uvMatrix, float coverage,
                                           AAType aaType);

 protected:
  PlacementPtr<GeometryProcessor> onMakeGeometryProcessor(RenderTarget* renderTarget) override;

  void onDraw(RenderPass* renderPass) override;

  Type type() override {
    return Type::HairlineLineOp;
  }

 private:
  HairlineLineOp(BlockAllocator* allocator, std::shared_ptr<GPUHairlineProxy> hairlineProxy,
                 PMColor color, const Matrix& uvMatrix, float coverage, AAType aaType);

  std::shared_ptr<GPUHairlineProxy> hairlineProxy;
  PMColor color;
  Matrix uvMatrix;
  float coverage = 1.0f;

  friend class BlockAllocator;
};

}  // namespace tgfx

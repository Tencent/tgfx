/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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
#include "gpu/AAType.h"
#include "gpu/proxies/AtlasProxy.h"
#include "tgfx/core/Shape.h"

namespace tgfx {
class AtlasTextDrawOp : public DrawOp {
 public:
  static PlacementNode<AtlasTextDrawOp> Make(std::shared_ptr<GpuBufferProxy>,
                                             const Matrix& uvMatrix, AAType aaType);

  AtlasTextDrawOp(std::shared_ptr<GpuBufferProxy>, const Matrix& uvMatrix, AAType aaType);
  void execute(RenderPass* renderPass) override;

 private:
  std::shared_ptr<GpuBufferProxy> atlasProxy = nullptr;
  Matrix uvMatrix = Matrix::I();
};
}  // namespace tgfx
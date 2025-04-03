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

#include "AtlasTextDrawOp.h"

#include "core/atlas/AtlasManager.h"
#include "core/utils/PlacementNode.h"

namespace tgfx {
PlacementNode<AtlasTextDrawOp> AtlasTextDrawOp::Make(std::shared_ptr<GpuBufferProxy> atlasProxy,
                                                     const Matrix& uvMatrix, AAType aaType) {
  if (atlasProxy == nullptr) {
    return nullptr;
  }
  auto drawingBuffer = atlasProxy->getContext()->drawingBuffer();
  return drawingBuffer->makeNode<AtlasTextDrawOp>(std::move(atlasProxy), uvMatrix, aaType);
}

AtlasTextDrawOp::AtlasTextDrawOp(std::shared_ptr<GpuBufferProxy> proxy, const Matrix& uvMatrix,
                                 AAType aaType)
    : DrawOp(aaType), atlasProxy(proxy), uvMatrix(uvMatrix) {
  // Initialize other members if needed
}

void AtlasTextDrawOp::execute(RenderPass* renderPass) {
  if (atlasProxy == nullptr || renderPass == nullptr) {
    return;
  }
  auto atlasManger = atlasProxy->getContext()->atlasManager();
  if (atlasManger == nullptr) {
    return;
  }
  atlasManger->uploadToTexture();

  // Implement the execution logic for the AtlasTextDrawOp
  // This will involve using the atlasProxy and uvMatrix to draw the text
  // on the render pass.
}

}  // namespace tgfx
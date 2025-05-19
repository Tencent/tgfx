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

#include "DstTextureCopyOp.h"
#include <InspectorDefine.h>
#include "core/utils/Log.h"
#include "gpu/Gpu.h"
#include "gpu/RenderPass.h"

namespace tgfx {
PlacementPtr<DstTextureCopyOp> DstTextureCopyOp::Make(std::shared_ptr<TextureProxy> textureProxy,
                                                      int srcX, int srcY) {
  if (textureProxy == nullptr) {
    return nullptr;
  }
  auto drawingBuffer = textureProxy->getContext()->drawingBuffer();
  return drawingBuffer->make<DstTextureCopyOp>(std::move(textureProxy), srcX, srcY);
}

DstTextureCopyOp::DstTextureCopyOp(std::shared_ptr<TextureProxy> textureProxy, int srcX, int srcY)
    : textureProxy(std::move(textureProxy)), srcX(srcX), srcY(srcY) {
}

void DstTextureCopyOp::execute(RenderPass* renderPass) {
  OperateMark(OpTaskType::DstTextureCopyOp);
  auto texture = textureProxy->getTexture();
  if (texture == nullptr) {
    LOGE("CopyOp::execute() Failed to get the dest texture!");
    return;
  }
  renderPass->copyToTexture(texture.get(), srcX, srcY);
}

}  // namespace tgfx

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

#include "CopyOp.h"
#include "core/utils/Log.h"
#include "gpu/Gpu.h"
#include "gpu/RenderPass.h"

namespace tgfx {
std::unique_ptr<CopyOp> CopyOp::Make(std::shared_ptr<TextureProxy> textureProxy,
                                     const Rect& srcRect, const Point& dstPoint) {
  if (textureProxy == nullptr || srcRect.isEmpty()) {
    return nullptr;
  }
  return std::unique_ptr<CopyOp>(new CopyOp(std::move(textureProxy), srcRect, dstPoint));
}

CopyOp::CopyOp(std::shared_ptr<TextureProxy> textureProxy, const Rect& srcRect,
               const Point& dstPoint)
    : Op(srcRect), textureProxy(std::move(textureProxy)), srcRect(srcRect), dstPoint(dstPoint) {
}

void CopyOp::execute(RenderPass* renderPass) {
  auto texture = textureProxy->getTexture();
  if (texture == nullptr) {
    LOGE("CopyOp::execute() Failed to get the dest texture!");
    return;
  }
  renderPass->copyTo(texture.get(), srcRect, dstPoint);
}

}  // namespace tgfx

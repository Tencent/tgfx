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

#include "Op.h"
#include "gpu/proxies/TextureProxy.h"

namespace tgfx {
/**
 * DstTextureCopyOp is an operation that copies a portion of a render target to the given texture.
 */
class DstTextureCopyOp : public Op {
 public:
  static PlacementPtr<DstTextureCopyOp> Make(std::shared_ptr<TextureProxy> textureProxy, int srcX,
                                             int srcY);

  void execute(RenderPass* renderPass) override;

 private:
  std::shared_ptr<TextureProxy> textureProxy = nullptr;
  int srcX = 0;
  int srcY = 0;

  DstTextureCopyOp(std::shared_ptr<TextureProxy> textureProxy, int srcX, int srcY);

  friend class BlockBuffer;
};
}  // namespace tgfx
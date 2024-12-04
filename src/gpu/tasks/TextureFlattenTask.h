/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "gpu/proxies/RenderTargetProxy.h"

namespace tgfx {
class TextureFlattenTask {
 public:
  /**
   * Prepares the task for execution. Returns false if the task can be skipped.
   */
  bool prepare(Context* context);

  /**
   * Executes the task to flatten the texture.
   */
  bool execute(Context* context);

 private:
  UniqueKey uniqueKey = {};
  std::shared_ptr<TextureProxy> sourceTextureProxy = nullptr;
  std::shared_ptr<Texture> flatTexture = nullptr;
  std::shared_ptr<RenderTarget> renderTarget = nullptr;

  TextureFlattenTask(UniqueKey uniqueKey, std::shared_ptr<TextureProxy> textureProxy);

  friend class ProxyProvider;
};
}  // namespace tgfx

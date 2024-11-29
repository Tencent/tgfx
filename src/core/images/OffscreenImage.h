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

#include "core/images/ResourceImage.h"
#include "gpu/proxies/RenderTargetProxy.h"

namespace tgfx {
/**
 * OffscreenImage is an image that draws something offscreen and caches the result.
 */
class OffscreenImage : public ResourceImage {
 public:
  explicit OffscreenImage(UniqueKey uniqueKey);

  bool isYUV() const override {
    return false;
  }

  bool isFlat() const override {
    return true;
  }

 protected:
  std::shared_ptr<TextureProxy> onLockTextureProxy(const TPArgs& args,
                                                   const UniqueKey& key) const override final;

  virtual bool onDraw(std::shared_ptr<RenderTargetProxy> renderTarget,
                      uint32_t renderFlags) const = 0;
};
}  // namespace tgfx

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

#include "gpu/proxies/DrawableProxy.h"

namespace tgfx {
class QGLDrawableProxy : public DrawableProxy {
 public:
  QGLDrawableProxy(Context* context, Drawable* drawable);

  bool externallyOwned() const override;
  std::shared_ptr<TextureView> getTextureView() const override;
  std::shared_ptr<TextureProxy> asTextureProxy() const override;
  std::shared_ptr<RenderTarget> getRenderTarget() const override;

 private:
  mutable std::shared_ptr<RenderTargetProxy> textureRTProxy = nullptr;

  void ensureTextureRTProxy() const;
};
}  // namespace tgfx

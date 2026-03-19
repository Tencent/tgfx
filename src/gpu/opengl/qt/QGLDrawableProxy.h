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

#include "gpu/proxies/RenderTargetProxy.h"

namespace tgfx {
class QGLWindow;

class QGLDrawableProxy : public RenderTargetProxy {
 public:
  QGLDrawableProxy(Context* context, int width, int height, PixelFormat format, int sampleCount,
                   ImageOrigin origin, QGLWindow* window);

  Context* getContext() const override;
  int width() const override;
  int height() const override;
  PixelFormat format() const override;
  int sampleCount() const override;
  ImageOrigin origin() const override;
  bool externallyOwned() const override;
  std::shared_ptr<TextureView> getTextureView() const override;
  std::shared_ptr<TextureProxy> asTextureProxy() const override;
  std::shared_ptr<RenderTarget> getRenderTarget() const override;

  std::shared_ptr<RenderTargetProxy> getTextureTargetProxy() const;
  void releaseTexture();

 private:
  Context* _context = nullptr;
  int _width = 0;
  int _height = 0;
  PixelFormat _format = PixelFormat::RGBA_8888;
  int _sampleCount = 1;
  ImageOrigin _origin = ImageOrigin::BottomLeft;
  QGLWindow* _window = nullptr;
  mutable std::shared_ptr<RenderTargetProxy> textureRTProxy = nullptr;
  std::weak_ptr<RenderTargetProxy> weakThis;

  void ensureTextureRTProxy() const;

  friend class QGLWindow;
};
}  // namespace tgfx

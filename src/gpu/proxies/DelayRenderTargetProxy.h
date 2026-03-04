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
/**
 * An interface for providing render targets on demand. Implementations can defer the actual render
 * target creation until it is needed, for example, acquiring a Metal drawable at flush time.
 */
class RenderTargetProvider {
 public:
  virtual ~RenderTargetProvider() = default;

  /**
   * Creates or retrieves a render target. Called by DelayRenderTargetProxy when the render target is
   * first needed during a frame.
   */
  virtual std::shared_ptr<RenderTarget> getRenderTarget(Context* context) = 0;
};

/**
 * A RenderTargetProxy that defers the creation of the underlying render target until
 * getRenderTarget() is called. The provider is invoked at most once per frame; call reset() after
 * presenting to allow a new render target to be acquired on the next frame.
 */
class DelayRenderTargetProxy : public RenderTargetProxy {
 public:
  DelayRenderTargetProxy(Context* context, int width, int height, PixelFormat format,
                         ImageOrigin origin, std::shared_ptr<RenderTargetProvider> provider);

  Context* getContext() const override;
  int width() const override;
  int height() const override;
  PixelFormat format() const override;
  int sampleCount() const override;
  ImageOrigin origin() const override;
  bool externallyOwned() const override;
  std::shared_ptr<TextureView> getTextureView() const override;
  std::shared_ptr<RenderTarget> getRenderTarget() const override;

  /**
   * Resets the cached render target. The next call to getRenderTarget() will re-invoke the provider
   * to acquire a new render target.
   */
  void reset();

 private:
  Context* _context = nullptr;
  int _width = 0;
  int _height = 0;
  PixelFormat _format = PixelFormat::RGBA_8888;
  ImageOrigin _origin = ImageOrigin::TopLeft;
  std::shared_ptr<RenderTargetProvider> _provider = nullptr;
  mutable std::shared_ptr<RenderTarget> _renderTarget = nullptr;
};
}  // namespace tgfx

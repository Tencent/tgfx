/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "ResourceProxy.h"
#include "TextureProxy.h"
#include "gpu/RenderTarget.h"

namespace tgfx {
/**
 * This class defers the acquisition of render targets until they are actually required.
 */
class RenderTargetProxy : public ResourceProxy {
 public:
  /**
   * Creates a new RenderTargetProxy with an existing backend render target.
   */
  static std::shared_ptr<RenderTargetProxy> MakeFrom(Context* context,
                                                     const BackendRenderTarget& backendRenderTarget,
                                                     ImageOrigin origin = ImageOrigin::TopLeft);

  /**
   * Creates a new RenderTargetProxy with an existing backend texture. If adopted is true, the
   * backend texture will be destroyed at a later point after the proxy is released.
   */
  static std::shared_ptr<RenderTargetProxy> MakeFrom(Context* context,
                                                     const BackendTexture& backendTexture,
                                                     int sampleCount = 1,
                                                     ImageOrigin origin = ImageOrigin::TopLeft,
                                                     bool adopted = false);

  /**
   * Creates a new RenderTargetProxy with an existing HardwareBuffer.
   */
  static std::shared_ptr<RenderTargetProxy> MakeFrom(Context* context,
                                                     HardwareBufferRef hardwareBuffer,
                                                     int sampleCount = 1);

  /**
   * Creates a new RenderTargetProxy instance with specified context, with, height, format, sample
   * count, mipmap state and origin. If clearAll is true, the entire render target will be cleared
   * to transparent black.
   */
  static std::shared_ptr<RenderTargetProxy> Make(Context* context, int width, int height,
                                                 PixelFormat format = PixelFormat::RGBA_8888,
                                                 int sampleCount = 1, bool mipmapped = false,
                                                 ImageOrigin origin = ImageOrigin::TopLeft,
                                                 bool clearAll = false);

  /**
   * Creates a new RenderTargetProxy instance with the specified context, width, height, sample
   * count, mipmap state, and origin. If `isAlphaOnly` is true, it will try to use the ALPHA_8
   * format and fall back to RGBA_8888 if not supported. Otherwise, it will use the RGBA_8888
   * format. If clearAll is true, the entire render target will be cleared to transparent black.
   */
  static std::shared_ptr<RenderTargetProxy> MakeFallback(Context* context, int width, int height,
                                                         bool isAlphaOnly, int sampleCount = 1,
                                                         bool mipmapped = false,
                                                         ImageOrigin origin = ImageOrigin::TopLeft,
                                                         bool clearAll = false);

  /**
   * Returns the width of the render target.
   */
  int width() const {
    return _width;
  }

  /**
   * Returns the height of the render target.
   */
  int height() const {
    return _height;
  }

  /**
   * Returns the bounds of the render target.
   */
  Rect bounds() const {
    return Rect::MakeWH(_width, _height);
  }

  /**
   * Returns the pixel format of the render target.
   */
  PixelFormat format() const {
    return _format;
  }

  /**
   * If we are instantiated and have a render target, return the sampleCount value of that render
   * target. Otherwise, returns the proxy's sampleCount value from creation time.
   */
  int sampleCount() const {
    return _sampleCount;
  }

  /**
   * Returns the origin of the render target, either ImageOrigin::TopLeft or
   * ImageOrigin::BottomLeft.
   */
  ImageOrigin origin() const {
    return _origin;
  }

  /**
   * Returns the associated TextureProxy instance. Returns nullptr if the RenderTargetProxy is not
   * backed by a texture.
   */
  virtual std::shared_ptr<TextureProxy> getTextureProxy() const {
    return nullptr;
  }

  /**
   * Returns true if the proxy was backed by a texture.
   */
  bool isTextureBacked() const;

  /**
   * Returns the backing Texture of the proxy. Returns nullptr if the proxy is not instantiated yet,
   * or it is not backed by a texture.
   */
  std::shared_ptr<Texture> getTexture() const;

  /**
   * Returns the RenderTarget of the proxy. Returns nullptr if the proxy is not instantiated yet.
   */
  std::shared_ptr<RenderTarget> getRenderTarget() const;

  /**
   * Creates a compatible TextureProxy instance matches the properties of the RenderTargetProxy.
   */
  std::shared_ptr<TextureProxy> makeTextureProxy() const {
    return makeTextureProxy(width(), height());
  }

  /**
   * Creates a compatible TextureProxy instance of the specified size that matches the properties of
   * the RenderTargetProxy.
   */
  std::shared_ptr<TextureProxy> makeTextureProxy(int width, int height) const;

  /**
   * Creates a compatible RenderTargetProxy instance matches the properties of this one. If clearAll
   * is true, the returned render target will be cleared to transparent black.
   */
  std::shared_ptr<RenderTargetProxy> makeRenderTargetProxy(bool clearAll = false) const {
    return makeRenderTargetProxy(width(), height(), clearAll);
  }

  /**
   * Creates a compatible RenderTargetProxy instance of the specified size that matches the
   * properties of this one. If clearAll is true, the returned render target will be cleared to
   * transparent black.
   */
  std::shared_ptr<RenderTargetProxy> makeRenderTargetProxy(int width, int height,
                                                           bool clearAll = false) const;

 protected:
  RenderTargetProxy(UniqueKey uniqueKey, int width, int height, PixelFormat format, int sampleCount,
                    ImageOrigin origin);

 private:
  int _width = 0;
  int _height = 0;
  PixelFormat _format = PixelFormat::RGBA_8888;
  int _sampleCount = 1;
  ImageOrigin _origin = ImageOrigin::TopLeft;

  static std::shared_ptr<RenderTargetProxy> Create(Context* context, int width, int height,
                                                   PixelFormat format, int sampleCount,
                                                   bool mipmapped, ImageOrigin origin,
                                                   bool clearAll);

  friend class ProxyProvider;
};
}  // namespace tgfx

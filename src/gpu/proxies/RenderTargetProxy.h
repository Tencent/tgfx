/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "TextureProxy.h"
#include "gpu/BackingFit.h"
#include "gpu/resources/RenderTarget.h"
#include "tgfx/core/ColorSpace.h"

namespace tgfx {
/**
 * This class defers the acquisition of render targets until they are actually required.
 */
class RenderTargetProxy {
 public:
  /**
   * Wraps a backend renderTarget into RenderTargetProxy. The caller must ensure the backend
   * renderTarget is valid for the lifetime of the returned RenderTarget. Returns nullptr if the
   * context is nullptr or the backend renderTarget is invalid.
   */
  static std::shared_ptr<RenderTargetProxy> MakeFrom(
      Context* context, const BackendRenderTarget& backendRenderTarget,
      ImageOrigin origin = ImageOrigin::TopLeft,
      std::shared_ptr<ColorSpace> colorSpace = ColorSpace::MakeSRGB());
  /**
   * Creates a new RenderTargetProxy instance with the specified context, width, height, sample
   * count, mipmap state, and origin. If `isAlphaOnly` is true, it will try to use the ALPHA_8
   * format and fall back to RGBA_8888 if not supported. Otherwise, it will use the RGBA_8888
   * format.
   */
  static std::shared_ptr<RenderTargetProxy> MakeFallback(
      Context* context, int width, int height, bool alphaOnly, int sampleCount = 1,
      bool mipmapped = false, ImageOrigin origin = ImageOrigin::TopLeft,
      BackingFit backingFit = BackingFit::Exact,
      std::shared_ptr<ColorSpace> colorSpace = ColorSpace::MakeSRGB());

  virtual ~RenderTargetProxy() = default;

  /**
   * Returns the context associated with the RenderTarget.
   */
  virtual Context* getContext() const = 0;

  /**
   * Returns the width of the render target.
   */
  virtual int width() const = 0;

  /**
   * Returns the height of the render target.
   */
  virtual int height() const = 0;

  /**
   * Returns the bounds of the render target.
   */
  Rect bounds() const {
    return Rect::MakeWH(width(), height());
  }

  /**
   * Returns the pixel format of the render target.
   */
  virtual PixelFormat format() const = 0;

  /**
   * If we are instantiated and have a render target, return the sampleCount value of that render
   * target. Otherwise, returns the proxy's sampleCount value from creation time.
   */
  virtual int sampleCount() const = 0;

  /**
   * Returns the origin of the render target, either ImageOrigin::TopLeft or
   * ImageOrigin::BottomLeft.
   */
  virtual ImageOrigin origin() const = 0;

  /**
   * Returns true if the render target is externally owned.
   */
  virtual bool externallyOwned() const = 0;

  /**
   * Returns a reference to the underlying TextureProxy representation of this render target, may be
   * nullptr.
   */
  virtual std::shared_ptr<TextureProxy> asTextureProxy() const {
    return nullptr;
  }

  /**
   * Returns the TextureView associated with the RenderTargetProxy. Returns nullptr if the proxy is
   * not instantiated yet, or it is not backed by a texture view.
   */
  virtual std::shared_ptr<TextureView> getTextureView() const = 0;

  /**
   * Returns the RenderTarget of the proxy. Returns nullptr if the proxy is not instantiated yet.
   */
  virtual std::shared_ptr<RenderTarget> getRenderTarget() const = 0;

  /**
   * Returns the colorSpace of the proxy.
   */
  virtual std::shared_ptr<ColorSpace> getColorSpace() const = 0;

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
   * Creates a compatible RenderTargetProxy instance matches the properties of this one.
   */
  std::shared_ptr<RenderTargetProxy> makeRenderTargetProxy() const {
    return makeRenderTargetProxy(width(), height());
  }

  /**
   * Creates a compatible RenderTargetProxy instance of the specified size that matches the
   * properties of this one.
   */
  std::shared_ptr<RenderTargetProxy> makeRenderTargetProxy(int width, int height) const;

  /**
   * Returns a transformation matrix that maps the render target's coordinate system to the
   * destination coordinate system. The matrix is identity for ImageOrigin::TopLeft, and flips the
   * Y-axis for ImageOrigin::BottomLeft.
   */
  Matrix getOriginTransform() const;
};
}  // namespace tgfx

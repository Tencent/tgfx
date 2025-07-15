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

#include "ResourceProxy.h"
#include "gpu/Texture.h"
#include "gpu/opengl/GLCaps.h"

namespace tgfx {
class RenderTargetProxy;

/**
 * This class defers the acquisition of textures until they are actually required.
 */
class TextureProxy : public ResourceProxy {
 public:
  /**
   * Returns the width of the texture.
   */
  virtual int width() const {
    return _width;
  }

  /**
   * Returns the height of the texture.
   */
  virtual int height() const {
    return _height;
  }

  /**
   * Returns the width of the backing store, which may differ from the texture width if the texture
   * is approximate size.
   */
  virtual int backingStoreWidth() const {
    return _width;
  }

  /**
   * Returns the height of the backing store, which may differ from the texture height if the
   * texture is approximate size.
   */
  virtual int backingStoreHeight() const {
    return _height;
  }

  /**
   * Returns the origin of the texture, either ImageOrigin::TopLeft or ImageOrigin::BottomLeft.
   */
  virtual ImageOrigin origin() const {
    return _origin;
  }

  /**
   * Return the mipmap state of the texture.
   */
  virtual bool hasMipmaps() const {
    return _mipmapped;
  }

  /**
   * Returns true if the texture represents transparency only.
   */
  virtual bool isAlphaOnly() const {
    return _format == PixelFormat::ALPHA_8;
  }

  /**
   * Returns the underlying RenderTargetProxy if this TextureProxy is also a render target proxy;
   * otherwise, returns nullptr.
   */
  virtual std::shared_ptr<RenderTargetProxy> asRenderTargetProxy() const {
    return nullptr;
  }

  /**
   * Returns the associated Texture instance.
   */
  virtual std::shared_ptr<Texture> getTexture() const {
    return std::static_pointer_cast<Texture>(resource);
  }

 protected:
  int _width = 0;
  int _height = 0;
  PixelFormat _format = PixelFormat::RGBA_8888;
  bool _mipmapped = false;
  ImageOrigin _origin = ImageOrigin::TopLeft;

  TextureProxy(int width, int height, PixelFormat pixelFormat, bool mipmapped = false,
               ImageOrigin origin = ImageOrigin::TopLeft)
      : _width(width), _height(height), _format(pixelFormat), _mipmapped(mipmapped),
        _origin(origin) {
  }

  friend class ProxyProvider;
};
}  // namespace tgfx

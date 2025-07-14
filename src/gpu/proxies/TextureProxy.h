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
  virtual int width() const = 0;

  /**
   * Returns the height of the texture.
   */
  virtual int height() const = 0;

  /**
   * Returns the origin of the texture, either ImageOrigin::TopLeft or ImageOrigin::BottomLeft.
   */
  virtual ImageOrigin origin() const = 0;

  /**
   * Return the mipmap state of the texture.
   */
  virtual bool hasMipmaps() const = 0;

  /**
   * Returns true if the texture represents transparency only.
   */
  virtual bool isAlphaOnly() const = 0;

  /**
   * Returns true if it is a flatten texture proxy.
   */
  virtual bool isFlatten() const {
    return false;
  }

  /**
   * Returns the associated Texture instance.
   */
  virtual std::shared_ptr<Texture> getTexture() const = 0;

  /**
   * Returns the underlying RenderTargetProxy if this TextureProxy is also a render target proxy;
   * otherwise, returns nullptr.
   */
  virtual std::shared_ptr<RenderTargetProxy> asRenderTargetProxy() const {
    return nullptr;
  }

 protected:
  explicit TextureProxy(UniqueKey uniqueKey) : ResourceProxy(std::move(uniqueKey)) {
  }

 private:
  friend class ProxyProvider;
};
}  // namespace tgfx

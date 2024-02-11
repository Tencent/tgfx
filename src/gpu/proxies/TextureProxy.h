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
#include "gpu/Texture.h"
#include "gpu/TextureSampler.h"

namespace tgfx {
/**
 * This class defers the acquisition of textures until they are actually required.
 */
class TextureProxy : public ResourceProxy {
 public:
  /**
   * Returns the width of the texture.
   */
  int width() const {
    return _width;
  }

  /**
   * Returns the height of the texture.
   */
  int height() const {
    return _height;
  }

  /**
   * Return the mipmap state of the texture.
   */
  bool hasMipmaps() const {
    return mipmapped;
  }

  /**
   * Returns the origin of the texture, either ImageOrigin::TopLeft or ImageOrigin::BottomLeft.
   */
  ImageOrigin origin() const {
    return _origin;
  }

  /**
   * Returns true if the texture represents transparency only.
   */
  bool isAlphaOnly() const {
    return _isAlphaOnly;
  }

  /**
   * Returns true if the backend texture is externally owned.
   */
  bool externallyOwned() const {
    return _externallyOwned;
  }

  /**
   * Returns the associated Texture instance.
   */
  std::shared_ptr<Texture> getTexture() const;

 private:
  int _width = 0;
  int _height = 0;
  bool mipmapped = false;
  bool _isAlphaOnly = false;
  ImageOrigin _origin = ImageOrigin::TopLeft;
  bool _externallyOwned = false;

  TextureProxy(ResourceKey resourceKey, int width, int height, bool mipmapped, bool isAlphaOnly,
               ImageOrigin origin = ImageOrigin::TopLeft, bool externallyOwned = false);

  friend class ProxyProvider;
};
}  // namespace tgfx

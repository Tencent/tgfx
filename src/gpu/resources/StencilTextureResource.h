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

#include "core/utils/UniqueID.h"
#include "gpu/resources/Resource.h"
#include "tgfx/gpu/Texture.h"

namespace tgfx {
/**
 * StencilTextureResource is a resource that encapsulates a depth/stencil texture used as a
 * render attachment. Instances are cached by their (width, height) so that successive draw
 * operations of the same surface size reuse the same GPU texture.
 */
class StencilTextureResource : public Resource {
 public:
  /**
   * Computes a ScratchKey for a stencil texture with the given dimensions.
   */
  static ScratchKey ComputeScratchKey(int width, int height);

  /**
   * Finds a reusable StencilTextureResource from the cache or creates a new one. The returned
   * texture has the format PixelFormat::DEPTH24_STENCIL8 and the usage flag
   * TextureUsage::RENDER_ATTACHMENT. Returns nullptr if texture creation fails.
   */
  static std::shared_ptr<StencilTextureResource> FindOrCreate(Context* context, int width,
                                                              int height);

  size_t memoryUsage() const override {
    return static_cast<size_t>(texture->width()) * static_cast<size_t>(texture->height()) * 4;
  }

  /**
   * Returns the width of the underlying texture in pixels.
   */
  int width() const {
    return texture->width();
  }

  /**
   * Returns the height of the underlying texture in pixels.
   */
  int height() const {
    return texture->height();
  }

  /**
   * Returns the underlying GPU texture.
   */
  std::shared_ptr<Texture> getTexture() const {
    return texture;
  }

 private:
  std::shared_ptr<Texture> texture = nullptr;

  explicit StencilTextureResource(std::shared_ptr<Texture> texture) : texture(std::move(texture)) {
  }
};
}  // namespace tgfx

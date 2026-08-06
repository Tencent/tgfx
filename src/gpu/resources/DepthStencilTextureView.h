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

#include "gpu/resources/DefaultTextureView.h"

namespace tgfx {
/**
 * A cacheable TextureView wrapping a depth/stencil attachment texture.
 */
class DepthStencilTextureView : public DefaultTextureView {
 public:
  size_t memoryUsage() const override;

  /**
   * Returns the shared depth/stencil attachment for the given size and sample count, creating
   * it on first request. Calls with equal size and sample count always return the same cached
   * instance.
   * @param context The GPU context that owns the texture and the resource cache.
   * @param width The width of the depth/stencil texture in pixels.
   * @param height The height of the depth/stencil texture in pixels.
   * @param sampleCount The sample count of the depth/stencil texture.
   * @return The shared attachment for the given size and sample count, or nullptr if the
   * texture allocation fails.
   */
  static std::shared_ptr<DepthStencilTextureView> Make(Context* context, int width, int height,
                                                       int sampleCount);

  /**
   * Returns the sample count of the depth/stencil texture.
   */
  int sampleCount() const {
    return _texture->sampleCount();
  }

 private:
  DepthStencilTextureView(std::shared_ptr<Texture> texture);
};

}  // namespace tgfx

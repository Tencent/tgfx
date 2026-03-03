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

#include <Metal/Metal.h>
#include "MetalResource.h"
#include "tgfx/gpu/Texture.h"

namespace tgfx {

class MetalGPU;

/**
 * Metal texture implementation.
 */
class MetalTexture : public Texture, public MetalResource {
 public:
  static std::shared_ptr<MetalTexture> Make(MetalGPU* gpu, const TextureDescriptor& descriptor);

  /**
   * Creates a MetalTexture wrapper from an external Metal texture.
   */
  static std::shared_ptr<MetalTexture> MakeFrom(MetalGPU* gpu, id<MTLTexture> metalTexture,
                                                uint32_t usage, bool adopted);

  /**
   * Returns the Metal texture.
   */
  id<MTLTexture> metalTexture() const {
    return texture;
  }

  // Texture interface implementation
  BackendTexture getBackendTexture() const override;
  BackendRenderTarget getBackendRenderTarget() const override;

 protected:
  MetalTexture(const TextureDescriptor& descriptor, id<MTLTexture> metalTexture);

  ~MetalTexture() override = default;

  void onRelease(MetalGPU* gpu) override;

  virtual void onReleaseTexture();

  id<MTLTexture> texture = nil;

  friend class MetalGPU;
};

}  // namespace tgfx
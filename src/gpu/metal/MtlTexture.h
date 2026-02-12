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
#include "tgfx/gpu/Texture.h"
#include "MtlResource.h"

namespace tgfx {

class MtlGPU;

/**
 * Metal texture implementation.
 */
class MtlTexture : public Texture, public MtlResource {
 public:
  static std::shared_ptr<MtlTexture> Make(MtlGPU* gpu, const TextureDescriptor& descriptor);

  /**
   * Creates a MtlTexture wrapper from an external Metal texture.
   */
  static std::shared_ptr<MtlTexture> MakeFrom(MtlGPU* gpu, id<MTLTexture> mtlTexture,
                                              uint32_t usage, bool adopted);

  /**
   * Returns the Metal texture.
   */
  id<MTLTexture> mtlTexture() const {
    return texture;
  }

  // Texture interface implementation
  BackendTexture getBackendTexture() const override;
  BackendRenderTarget getBackendRenderTarget() const override;

 protected:
  MtlTexture(const TextureDescriptor& descriptor, id<MTLTexture> mtlTexture);

  ~MtlTexture() override = default;

  void onRelease(MtlGPU* gpu) override;

  virtual void onReleaseTexture();

  id<MTLTexture> texture = nil;

  friend class MtlGPU;
};

}  // namespace tgfx
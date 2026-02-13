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

#include <CoreVideo/CoreVideo.h>
#include "MetalTexture.h"

namespace tgfx {
class MetalGPU;

/**
 * Metal texture implementation backed by a CVPixelBuffer hardware buffer.
 */
class MetalHardwareTexture : public MetalTexture {
 public:
  /**
   * Creates Metal textures from a CVPixelBuffer. Returns a vector of textures for multi-plane
   * formats (like YUV), or a single texture for standard formats. Returns an empty vector if
   * creation fails.
   */
  static std::vector<std::shared_ptr<Texture>> MakeFrom(MetalGPU* gpu, CVPixelBufferRef pixelBuffer,
                                                        uint32_t usage,
                                                        CVMetalTextureCacheRef textureCache);

  ~MetalHardwareTexture() override = default;

  HardwareBufferRef getHardwareBuffer() const override {
    return pixelBuffer;
  }

 private:
  MetalHardwareTexture(const TextureDescriptor& descriptor, id<MTLTexture> mtlTexture,
                     CVPixelBufferRef pixelBuffer, CVMetalTextureRef cvMetalTexture);

  void onRelease(MetalGPU* gpu) override;

  void onReleaseTexture() override;

  CVPixelBufferRef pixelBuffer = nullptr;
  CVMetalTextureRef cvMetalTexture = nil;

  friend class MetalGPU;
};
}  // namespace tgfx

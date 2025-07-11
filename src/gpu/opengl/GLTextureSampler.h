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

#include "gpu/TextureSampler.h"
#include "tgfx/gpu/opengl/GLDefines.h"

namespace tgfx {
/**
 * GLSampler is a TextureSampler that wraps an OpenGL texture sampler, providing access to its
 * OpenGL texture ID and target.
 */
class GLTextureSampler : public TextureSampler {
 public:
  GLTextureSampler(unsigned id, unsigned target, PixelFormat format, int maxMipmapLevel = 0)
      : TextureSampler(format, maxMipmapLevel), _id(id), _target(target) {
  }

  /**
   * Returns the OpenGL texture ID for this sampler.
   */
  unsigned id() const {
    return _id;
  }

  /**
   * Returns the OpenGL texture target for this sampler.
   */
  unsigned target() const {
    return _target;
  }

  SamplerType type() const override;

  BackendTexture getBackendTexture(int width, int height) const override;

  void writePixels(Context* context, const Rect& rect, const void* pixels,
                   size_t rowBytes) override;

  void regenerateMipmapLevels(Context* context) override;

  void computeSamplerKey(Context* context, BytesKey* bytesKey) const override;

  void releaseGPU(Context* context) override;

 protected:
  unsigned _id = 0;
  unsigned _target = GL_TEXTURE_2D;
};
}  // namespace tgfx

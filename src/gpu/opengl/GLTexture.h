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

#include "gpu/GPUTexture.h"
#include "tgfx/gpu/opengl/GLDefines.h"

namespace tgfx {
/**
 * GLTexture is a GPUTexture that wraps an OpenGL texture, providing access to its OpenGL texture ID
 * and target.
 */
class GLTexture : public GPUTexture {
 public:
  GLTexture(unsigned id, unsigned target, PixelFormat format, int mipLevelCount = 1)
      : GPUTexture(format, mipLevelCount), _id(id), _target(target) {
  }

  /**
   * Returns the OpenGL ID for this texture.
   */
  unsigned id() const {
    return _id;
  }

  /**
   * Returns the OpenGL target for this texture.
   */
  unsigned target() const {
    return _target;
  }

  TextureType type() const override;

  BackendTexture getBackendTexture(int width, int height) const override;

  void computeTextureKey(Context* context, BytesKey* bytesKey) const override;

  void release(GPU* gpu) override;

 protected:
  unsigned _id = 0;
  unsigned _target = GL_TEXTURE_2D;
};
}  // namespace tgfx

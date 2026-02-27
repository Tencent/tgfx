/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "gpu/opengl/GLTexture.h"

namespace tgfx {
class GLExternalTexture : public GLTexture {
 public:
  GLExternalTexture(const TextureDescriptor& descriptor, unsigned target, unsigned textureID,
                    unsigned frameBufferID = 0)
      : GLTexture(descriptor, target, textureID), _frameBufferID(frameBufferID) {
  }

  unsigned frameBufferID() const override {
    return _textureID > 0 ? GLTexture::frameBufferID() : _frameBufferID;
  }

  void onReleaseTexture(GLGPU*) override {
    // External textures are not owned by TGFX, so we do not release them.
  }

 private:
  unsigned _frameBufferID = 0;
};
}  // namespace tgfx

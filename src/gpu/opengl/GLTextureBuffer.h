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

#include "gpu/opengl/GLBuffer.h"
#include "gpu/opengl/GLState.h"
#include "gpu/opengl/GLTexture.h"

namespace tgfx {
/**
 * GLTextureBuffer is a readback buffer implementation that uses a texture as an intermediate buffer
 * to read pixels from the GPU when PBO is not supported.
 */
class GLTextureBuffer : public GLBuffer {
 public:
  GLTextureBuffer(std::shared_ptr<GLInterface> interface, std::shared_ptr<GLState> state,
                  size_t size);

  void* map(size_t offset, size_t size) override;

  void unmap() override;

  std::shared_ptr<GPUTexture> acquireTexture(GPU* gpu, std::shared_ptr<GPUTexture> srcTexture,
                                             const Rect& srcRect, size_t dstOffset,
                                             size_t dstRowBytes);

 private:
  std::shared_ptr<GLState> state = nullptr;
  std::shared_ptr<GPUTexture> texture = nullptr;
  size_t readOffset = 0;
  size_t readRowBytes = 0;
  void* bufferData = nullptr;
};
}  // namespace tgfx

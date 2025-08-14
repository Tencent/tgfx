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

#include "gpu/DefaultTextureView.h"
#include "gpu/opengl/GLFrameBuffer.h"

namespace tgfx {
class GLGPU;

class GLTextureFrameBuffer : public GLFrameBuffer {
 public:
  static std::unique_ptr<GLTextureFrameBuffer> MakeFrom(GLGPU* gpu, GPUTexture* texture, int width,
                                                        int height, int sampleCount);
  PixelFormat format() const override {
    return _format;
  }

  int sampleCount() const override {
    return _sampleCount;
  }

  unsigned readFrameBufferID() const override {
    return _readFrameBufferID;
  }

  unsigned drawFrameBufferID() const override {
    return _drawFrameBufferID;
  }

  void release(GPU* gpu) override;

 private:
  unsigned _readFrameBufferID = 0;
  unsigned _drawFrameBufferID = 0;
  unsigned renderBufferID = 0;
  PixelFormat _format = PixelFormat::RGBA_8888;
  int _sampleCount = 1;
  unsigned textureTarget = 0;

  GLTextureFrameBuffer(unsigned readFrameBufferID, unsigned drawFrameBufferID,
                       unsigned renderBufferID, PixelFormat format, int sampleCount,
                       unsigned textureTarget)
      : _readFrameBufferID(readFrameBufferID), _drawFrameBufferID(drawFrameBufferID),
        renderBufferID(renderBufferID), _format(format), _sampleCount(sampleCount),
        textureTarget(textureTarget) {
  }

  friend class RenderTarget;
};
}  // namespace tgfx

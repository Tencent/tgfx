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

#include "gpu/opengl/GLFrameBuffer.h"

namespace tgfx {
class GLExternalFrameBuffer : public GLFrameBuffer {
 public:
  PixelFormat format() const override {
    return _format;
  }

  int sampleCount() const override {
    return 1;
  }

  unsigned readFrameBufferID() const override {
    return frameBufferID;
  }

  unsigned drawFrameBufferID() const override {
    return frameBufferID;
  }

  void release(GPU*) override {
    // Do nothing, the external FBO is not owned by us.
  }

 private:
  unsigned frameBufferID = 0;
  PixelFormat _format = PixelFormat::RGBA_8888;

  GLExternalFrameBuffer(unsigned frameBufferID, PixelFormat format)
      : frameBufferID(frameBufferID), _format(format) {
  }

  friend class GLGPU;
};
}  // namespace tgfx

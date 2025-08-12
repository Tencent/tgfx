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

#include "gpu/opengl/GLRenderTarget.h"

namespace tgfx {
class GLExternalRenderTarget : public GLRenderTarget {
 public:
  GLExternalRenderTarget(Context* context, int width, int height, ImageOrigin origin,
                         PixelFormat format, unsigned frameBufferID)
      : context(context), _width(width), _height(height), _origin(origin), _format(format),
        frameBufferID(frameBufferID) {
  }

  Context* getContext() const override {
    return context;
  }

  int width() const override {
    return _width;
  }

  int height() const override {
    return _height;
  }

  ImageOrigin origin() const override {
    return _origin;
  }

  int sampleCount() const override {
    return 1;
  }

  PixelFormat format() const override {
    return _format;
  }

  bool externallyOwned() const override {
    return true;
  }

  unsigned readFrameBufferID() const override {
    return frameBufferID;
  }

  unsigned drawFrameBufferID() const override {
    return frameBufferID;
  }

 private:
  Context* context = nullptr;
  int _width = 0;
  int _height = 0;
  ImageOrigin _origin = ImageOrigin::TopLeft;
  PixelFormat _format = PixelFormat::RGBA_8888;
  unsigned frameBufferID = 0;
};
}  // namespace tgfx

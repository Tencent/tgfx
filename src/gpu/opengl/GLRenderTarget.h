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

#include "gpu/RenderTarget.h"

namespace tgfx {
class GLInterface;

/**
 * Represents an OpenGL 2D buffer of pixels that can be rendered to.
 */
class GLRenderTarget : public RenderTarget {
 public:
  PixelFormat format() const override {
    return pixelFormat;
  }

  /**
   * Returns the frame buffer id associated with this render target.
   */
  unsigned getFrameBufferID(bool forDraw = true) const;

  BackendRenderTarget getBackendRenderTarget() const override;

  bool readPixels(const ImageInfo& dstInfo, void* dstPixels, int srcX = 0,
                  int srcY = 0) const override;

 protected:
  void onReleaseGPU() override;

 private:
  PixelFormat pixelFormat = PixelFormat::RGBA_8888;
  unsigned frameBufferRead = 0;
  unsigned frameBufferDraw = 0;
  unsigned renderBufferID = 0;
  unsigned textureTarget = 0;
  bool externalResource = false;

  GLRenderTarget(int width, int height, ImageOrigin origin, int sampleCount, PixelFormat format,
                 unsigned textureTarget = 0);

  friend class RenderTarget;
};
}  // namespace tgfx

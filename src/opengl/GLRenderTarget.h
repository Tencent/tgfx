/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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
#include "opengl/GLFrameBuffer.h"

namespace tgfx {
class GLInterface;

/**
 * Represents an OpenGL 2D buffer of pixels that can be rendered to.
 */
class GLRenderTarget : public RenderTarget {
 public:
  PixelFormat format() const override {
    return frameBufferForDraw.format;
  }

  /**
   * Returns the frame buffer id associated with this render target.
   */
  unsigned getFrameBufferID(bool forDraw = true) const;

  void resolve() const;

  BackendRenderTarget getBackendRenderTarget() const override;

  bool readPixels(const ImageInfo& dstInfo, void* dstPixels, int srcX = 0,
                  int srcY = 0) const override;

 private:
  GLFrameBuffer frameBufferForRead = {};
  GLFrameBuffer frameBufferForDraw = {};
  unsigned msRenderBufferID = 0;
  unsigned textureTarget = 0;
  bool externalResource = false;

  GLRenderTarget(int width, int height, ImageOrigin origin, int sampleCount,
                 GLFrameBuffer frameBuffer, unsigned textureTarget = 0);

  void onReleaseGPU() override;

  friend class RenderTarget;
};
}  // namespace tgfx

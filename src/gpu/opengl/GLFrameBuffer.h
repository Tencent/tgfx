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

#include "gpu/GPUFrameBuffer.h"

namespace tgfx {
/**
 * Represents an OpenGL 2D buffer of pixels that can be rendered to.
 */
class GLFrameBuffer : public GPUFrameBuffer {
 public:
  /**
   * Returns the frame buffer ID used for reading pixels.
   */
  virtual unsigned readFrameBufferID() const = 0;

  /**
   * Returns the frame buffer ID used for drawing pixels.
   */
  virtual unsigned drawFrameBufferID() const = 0;

  BackendRenderTarget getBackendRenderTarget(int width, int height) const override;
};
}  // namespace tgfx

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

#include "gpu/GPUResource.h"
#include "tgfx/gpu/Backend.h"
#include "tgfx/gpu/PixelFormat.h"

namespace tgfx {
/**
 * GPUFrameBuffer represents a frame buffer in the GPU backend that can be rendered to.
 */
class GPUFrameBuffer : public GPUResource {
 public:
  /**
   * Returns the pixel format of the frame buffer.
   */
  virtual PixelFormat format() const = 0;

  /**
   * Returns the number of samples used by the frame buffer. Returns 1 if multisampling is disabled,
   * or the number of samples per pixel if enabled.
   */
  virtual int sampleCount() const = 0;

  /**
   * Retrieves the backend render target.
   */
  virtual BackendRenderTarget getBackendRenderTarget(int width, int height) const = 0;
};
}  // namespace tgfx

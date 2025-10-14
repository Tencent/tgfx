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

#include "tgfx/core/ImageInfo.h"
#include "tgfx/gpu/Context.h"

namespace tgfx {
class GPUBufferProxy;

/**
 * SurfaceReadback represents an asynchronous operation to read pixels from a Surface. Because this
 * process may involve GPU work, the pixel data might not be available immediately after starting
 * the readback. Use isReady() to check if the pixel data is accessible, and lockPixels() to obtain
 * a pointer to the data. After you finish accessing the pixels, call unlockPixels() to release
 * them. Create a SurfaceReadback object by calling Surface::asyncReadPixels().
 */
class SurfaceReadback {
 public:
  ~SurfaceReadback();

  /**
   * Returns the ImageInfo describing the pixel data.
   */
  ImageInfo info() const {
    return _info;
  }

  /**
   * Returns true if the pixel data is ready to access, false otherwise. The pixel data may not be
   * immediately available after starting the readback operation, as it can involve asynchronous
   * GPU processing. This method does not block the calling thread.
   */
  bool isReady(Context* context) const;

  /**
   * Locks the pixel data for access and returns a pointer to it. The pointer remains valid until
   * unlockPixels() is called. This method may block until the pixel data is ready, if the backend
   * supports blocking.
   * @param context The Context associated with the Surface from which pixels are being read.
   * @param flipY If true, the pixel data will be flipped vertically. This is useful when the
   * surface origin is bottom-left but the caller expects a top-left origin, or vice versa. The
   * flipping is done on the CPU and may affect performance.
   * @return nullptr if the pixel data is not ready and blocking is unsupported, or if an error
   * occurs during the readback operation.
   */
  const void* lockPixels(Context* context, bool flipY = false);

  /**
   * Unlocks the pixel data previously locked by lockPixels(). After calling this method, the
   * pointer returned by lockPixels() is no longer valid.
   */
  void unlockPixels(Context* context);

 private:
  ImageInfo _info = {};
  std::shared_ptr<GPUBufferProxy> proxy = nullptr;
  void* flipYPixels = nullptr;

  SurfaceReadback(const ImageInfo& info, std::shared_ptr<GPUBufferProxy> proxy)
      : _info(info), proxy(std::move(proxy)) {
  }

  friend class Surface;
};
}  // namespace tgfx

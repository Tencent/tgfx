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

#include "tgfx/core/Canvas.h"
#include "tgfx/core/ImageInfo.h"
#include "tgfx/core/RenderFlags.h"
#include "tgfx/gpu/Backend.h"
#include "tgfx/gpu/ImageOrigin.h"

namespace tgfx {
class Canvas;
class Context;
class RenderContext;
class RenderTargetProxy;

/**
 * The Surface class is responsible for managing the pixels that a Canvas draws into. The Surface
 * takes care of allocating a Canvas that will draw into the surface. Call surface->getCanvas() to
 * use that Canvas (but don't delete it, it is owned by the surface). The Surface always has
 * non-zero dimensions. If there is a request for a new surface, and either of the requested
 * dimensions is zero, then nullptr will be returned.
 */
class Surface {
 public:
  /**
   * Creates a new Surface on GPU indicated by context. Allocates memory for pixels based on the
   * width, height, and color type (alpha only). A Surface with MSAA enabled is returned if the
   * sample count is greater than 1. Return nullptr if the size is invalid or the alpha-only
   * textures are not renderable in the GPU backend.
   */
  static std::shared_ptr<Surface> Make(Context* context, int width, int height,
                                       bool alphaOnly = false, int sampleCount = 1,
                                       bool mipmapped = false, uint32_t renderFlags = 0);

  /**
   * Creates a new Surface on GPU indicated by context. Allocates memory for pixels based on the
   * width, height, and colorType. A Surface with MSAA enabled is returned if the sample count is
   * greater than 1. Return nullptr if the size is invalid or the colorType is not renderable in
   * the GPU backend.
   */
  static std::shared_ptr<Surface> Make(Context* context, int width, int height, ColorType colorType,
                                       int sampleCount = 1, bool mipmapped = false,
                                       uint32_t renderFlags = 0);

  /**
   * Wraps a backend render target into Surface. The caller must ensure the renderTarget is valid
   * for the lifetime of the returned Surface. Returns nullptr if the context is nullptr or the
   * renderTarget is invalid.
   */
  static std::shared_ptr<Surface> MakeFrom(Context* context,
                                           const BackendRenderTarget& renderTarget,
                                           ImageOrigin origin, uint32_t renderFlags = 0);

  /**
   * Wraps a BackendTexture into the Surface. The caller must ensure the texture is valid for the
   * lifetime of the returned Surface. If the sampleCount is greater than zero, creates an
   * intermediate MSAA Surface which is used for drawing backendTexture. Returns nullptr if the
   * context is nullptr or the texture is not renderable in the GPU backend.
   */
  static std::shared_ptr<Surface> MakeFrom(Context* context, const BackendTexture& backendTexture,
                                           ImageOrigin origin, int sampleCount = 1,
                                           uint32_t renderFlags = 0);

  /**
   * Creates a Surface from the platform-specific hardware buffer. For example, the hardware buffer
   * could be an AHardwareBuffer on the android platform or a CVPixelBufferRef on the apple
   * platform. The returned Surface takes a reference to the hardwareBuffer. Returns nullptr if the
   * context is nullptr or the hardwareBuffer is not renderable.
   */
  static std::shared_ptr<Surface> MakeFrom(Context* context, HardwareBufferRef hardwareBuffer,
                                           int sampleCount = 1, uint32_t renderFlags = 0);

  virtual ~Surface();

  /**
   * Retrieves the context associated with this Surface.
   */
  Context* getContext() const;

  /**
   * Returns the render flags associated with this Surface.
   */
  uint32_t renderFlags() const;

  /**
   * Returns the width of this surface.
   */
  int width() const;

  /**
   * Returns the height of this surface.
   */
  int height() const;

  /**
   * Returns the origin of this surface, either ImageOrigin::TopLeft or ImageOrigin::BottomLeft.
   */
  ImageOrigin origin() const;

  /**
   * Retrieves the backend render target that the surface renders to. The returned
   * BackendRenderTarget should be discarded if the Surface is drawn to or deleted.
   */
  BackendRenderTarget getBackendRenderTarget();

  /**
   * Retrieves the backend texture that the surface renders to. Returns an invalid BackendTexture if
   * the surface has no backend texture. The returned BackendTexture should be discarded if the
   * Surface is drawn to or deleted.
   */
  BackendTexture getBackendTexture();

  /**
   * Retrieves the backing hardware buffer. This method does not acquire any additional reference to
   * the returned hardware buffer. Returns nullptr if the surface is not created from a hardware
   * buffer.
   */
  HardwareBufferRef getHardwareBuffer();

  /**
   * Returns Canvas that draws into the Surface. Subsequent calls return the same Canvas. Canvas
   * returned is managed and owned by Surface, and is deleted when the Surface is deleted.
   */
  Canvas* getCanvas();

  /**
   * Returns an Image capturing the Surface contents. Subsequent drawings to the Surface contents
   * are not captured. This method would trigger immediate texture copying if the Surface has no
   * backing texture or the backing texture was allocated externally. For example, the Surface was
   * created from a BackendRenderTarget, a BackendTexture or a HardwareBuffer.
   */
  std::shared_ptr<Image> makeImageSnapshot();

  /**
   * Returns pixel at (x, y) as unpremultiplied color. Some color precision may be lost in the
   * conversion to unpremultiplied color; original pixel data may have additional precision. Returns
   * a transparent color if the point (x, y) is not contained by the Surface bounds.
   * @param x  column index, zero or greater, and less than width()
   * @param y  row index, zero or greater, and less than height()
   * @return   pixel converted to unpremultiplied color
   */
  Color getColor(int x, int y);

  /**
   * Copies a rect of pixels to dstPixels with specified ImageInfo. Copy starts at (srcX, srcY), and
   * does not exceed Surface (width(), height()). Pixels are copied only if pixel conversion is
   * possible. Returns true if pixels are copied to dstPixels.
   */
  bool readPixels(const ImageInfo& dstInfo, void* dstPixels, int srcX = 0, int srcY = 0);

  /**
   * Returns the unique ID of the Surface. The ID is unique among all Surfaces.
   */
  uint32_t uniqueID() const {
    return _uniqueID;
  }

 private:
  /**
  * Returns the version of the content. The version is changed when the content is changed.
  * The initial version is 1;
  */
  uint32_t contentVersion() const;

  uint32_t _uniqueID = 0;
  RenderContext* renderContext = nullptr;
  Canvas* canvas = nullptr;

  static std::shared_ptr<Surface> MakeFrom(std::shared_ptr<RenderTargetProxy> renderTargetProxy,
                                           uint32_t renderFlags = 0);

  Surface(std::shared_ptr<RenderTargetProxy> proxy, uint32_t renderFlags = 0);

  friend class PictureImage;
  friend class DisplayList;
};
}  // namespace tgfx
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

#include <cstdint>
#include "ColorSpace.h"
#include "tgfx/core/Data.h"
#include "tgfx/core/ImageInfo.h"
#include "tgfx/core/YUVColorSpace.h"
#include "tgfx/core/YUVData.h"
#include "tgfx/platform/HardwareBuffer.h"
#include "tgfx/platform/NativeImage.h"

namespace tgfx {
class Context;
class TextureView;
class BufferImage;

/**
 * Describes pixel delivery and availability, not pixel mutability or a caller's target frame.
 */
enum class ImageBufferState {
  /**
   * Deferred delivery has not completed, so pixels are not yet available.
   */
  Pending,
  /**
   * Valid pixels are available and remain unchanged until the buffer expires. A streaming buffer
   * may retain previous pixels; Ready does not imply that a new source image was consumed.
   */
  Ready,
  /**
   * The buffer is no longer usable for texture generation or redraw. This state is terminal.
   */
  Expired
};

/**
 * ImageBuffer describes a two-dimensional array of pixels and is optimized for creating textures.
 * Delivery state and timestamp can be queried across threads; pixel access and other properties
 * must follow the creator's synchronization and Context requirements. Delivered pixels remain
 * unchanged while the buffer is Ready, but deferred buffers may begin Pending and some buffers have
 * a limited lifetime. ImageReader buffers share a native texture and must not be redrawn after
 * expiration. Observing Ready does not reserve those pixels against later source reads.
 * Other ImageBuffers normally start Ready and only expire if explicitly stated by the creator.
 * The added virtual delivery queries change this class's ABI. Rebuild callers and subclasses with
 * the library; retaining source-compatible calls does not make existing binaries compatible.
 */
class ImageBuffer {
 public:
  /**
   * Creates an ImageBuffer from the platform-specific hardware buffer. For example, the hardware
   * buffer could be an AHardwareBuffer on the android platform or a CVPixelBufferRef on the apple
   * platform. The returned ImageBuffer takes a reference to the hardwareBuffer. The caller must
   * ensure the buffer content stays unchanged for the lifetime of the returned ImageBuffer. Returns
   * nullptr if the hardwareBuffer is nullptr or the hardwareBuffer contains only one plane, which
   * is not in the YUV format.
   */
  static std::shared_ptr<ImageBuffer> MakeFrom(HardwareBufferRef hardwareBuffer,
                                               YUVColorSpace colorSpace);

  /**
   * Creates an ImageBuffer from the platform-specific hardware buffer. For example, the hardware
   * buffer could be an AHardwareBuffer on the android platform or a CVPixelBufferRef on the apple
   * platform. The returned ImageBuffer takes a reference to the hardwareBuffer. The caller must
   * ensure the buffer content stays unchanged for the lifetime of the returned ImageBuffer. Returns
   * nullptr if the hardwareBuffer contains more than one plane or the hardwareBuffer is nullptr.
   */
  static std::shared_ptr<ImageBuffer> MakeFrom(HardwareBufferRef hardwareBuffer,
                                               std::shared_ptr<ColorSpace> colorSpace = nullptr);

  /**
   * Creates an ImageBuffer in the I420 format with the specified YUVData and YUVColorSpace. The
   * caller must ensure the yuvData stays unchanged for the lifetime of the returned ImageBuffer.
   * Returns nullptr if the yuvData is invalid.
   */
  static std::shared_ptr<ImageBuffer> MakeI420(
      std::shared_ptr<YUVData> yuvData, YUVColorSpace colorSpace = YUVColorSpace::BT601_LIMITED);

  /**
   * Creates an ImageBuffer in the NV12 format with the specified YUVData and YUVColorSpace. The
   * caller must ensure the yuvData stays unchanged for the lifetime of the returned ImageBuffer.
   * Returns nullptr if the yuvData is invalid.
   */
  static std::shared_ptr<ImageBuffer> MakeNV12(
      std::shared_ptr<YUVData> yuvData, YUVColorSpace colorSpace = YUVColorSpace::BT601_LIMITED);

  virtual ~ImageBuffer() = default;
  /**
   * Returns the width of the image buffer.
   */
  virtual int width() const = 0;

  /**
   * Returns the height of the image buffer.
   */
  virtual int height() const = 0;

  /**
   * Returns true if pixels represent transparency only. If true, each pixel is packed in 8 bits as
   * defined by ColorType::ALPHA_8.
   */
  virtual bool isAlphaOnly() const = 0;

  /**
   * Returns true if the buffer can no longer create textures or be used for redraw. Expiration does
   * not prevent metadata queries, which retain their original synchronization requirements.
   * The default implementation returns false.
   */
  virtual bool expired() const {
    return false;
  }

  /**
   * Returns the current delivery state, safely across threads. By default, returns Ready unless
   * expired(). Ready does not guarantee a target-frame match or prevent subsequent expiration.
   */
  virtual ImageBufferState state() const {
    return expired() ? ImageBufferState::Expired : ImageBufferState::Ready;
  }

  /**
   * Returns the delivered source presentation timestamp in nanoseconds, or -1 when absent. Zero
   * is valid; the default implementation returns -1. For deferred buffers, read after texture
   * generation completes and Ready is observed. An expired buffer may retain its historical
   * timestamp, which does not permit redraw or establish a match for a caller's target frame.
   */
  virtual int64_t timestamp() const {
    return -1;
  }

  /**
   * Returns the ColorSpace of this ImageBuffer, or nullptr if unspecified. For an ImageReader
   * buffer whose source updates its color space, serialize this query and use of the returned
   * reference with source texture reads.
   */
  virtual const std::shared_ptr<ColorSpace>& colorSpace() const = 0;

 protected:
  ImageBuffer() = default;

  /**
   * Returns true if the ImageBuffer is backed by a PixelBuffer, allowing pixel locking.
   */
  virtual bool isPixelBuffer() const {
    return false;
  }

  /**
   * Creates a new TextureView capturing the pixels of the ImageBuffer. The mipmapped parameter
   * specifies whether created texture must allocate mip map levels.
   */
  virtual std::shared_ptr<TextureView> onMakeTexture(Context* context, bool mipmapped) const = 0;

  friend class TextureView;
  friend class BufferImage;
};
}  // namespace tgfx

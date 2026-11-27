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
 * Describes whether the pixels of an ImageBuffer have been delivered.
 */
enum class ImageBufferStatus {
  /**
   * The pixels are available.
   */
  Ready,
  /**
   * The ImageBuffer has not been drawn yet.
   */
  Pending,
  /**
   * The requested pixels were not obtained in time. The last valid latched pixels can be drawn
   * instead; they do not establish a match for the request.
   */
  Timeout,
  /**
   * The pixels could not be read. Only known-valid prior pixels can be retained; unknown effects
   * prevent safely exposing the retained texture as a fallback.
   */
  Failed,
  /**
   * The ImageBuffer can no longer be drawn. See expired().
   */
  Expired
};

/**
 * ImageBuffer describes a two-dimensional array of pixels and is optimized for creating textures.
 * Status queries are safe across threads. Pixel access, metadata and returned references follow
 * the creator's synchronization and Context requirements. Streaming buffers can change pixels on
 * retries until Ready; their delivered content remains valid only until expiration. ImageReader
 * buffers share native storage, and later reads can invalidate previous buffers. Other buffers
 * normally start Ready and only expire if explicitly stated by their creator. Virtual delivery
 * interfaces change the ABI; rebuild callers and subclasses together with the library.
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
   * Returns true if the ImageBuffer is expired, indicating that it cannot create any new textures.
   * Expiration does not prevent metadata queries, which keep their original synchronization rules.
   */
  virtual bool expired() const {
    return false;
  }

  /**
   * Returns the delivery status. For an ImageReader buffer, query after drawing is flushed; Timeout
   * and Failed can change on a subsequent retry until the buffer expires. Other ImageBuffers return
   * Ready unless expired. A status query does not reserve pixels against another source read.
   */
  virtual ImageBufferStatus status() const {
    return expired() ? ImageBufferStatus::Expired : ImageBufferStatus::Ready;
  }

  /**
   * Returns the color space, or nullptr if unspecified. Streaming sources can update it during
   * texture reads; serialize this query and use of its reference with those reads.
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

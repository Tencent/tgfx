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

#include "tgfx/core/ImageInfo.h"
#include "tgfx/core/Size.h"

#if defined(__ANDROID__) || defined(ANDROID)
struct AHardwareBuffer;
#elif defined(__OHOS__)
struct OH_NativeBuffer;
#elif defined(__APPLE__)
struct __CVBuffer;
#endif

namespace tgfx {
#if defined(__ANDROID__) || defined(ANDROID)
typedef AHardwareBuffer* HardwareBufferRef;
#elif defined(__OHOS__)
typedef OH_NativeBuffer* HardwareBufferRef;
#elif defined(__APPLE__)
// __CVBuffer == CVPixelBufferRef
typedef __CVBuffer* HardwareBufferRef;
#else
typedef void* HardwareBufferRef;
#endif

/**
 * Defines the possible pixel formats for a hardware buffer.
 */
enum class HardwareBufferFormat {
  /**
   * Uninitialized.
   */
  Unknown,

  /**
   * Each pixel is stored as a single translucency (alpha) channel. This is very useful for
   * storing masks efficiently, for instance. No color information is stored. With this
   * configuration, each pixel requires 1 byte of memory.
   */
  ALPHA_8,

  /**
   * Each pixel is stored on 4 bytes. Each channel (RGB and alpha for translucency) is stored with 8
   * bits of precision (256 possible values). The channel order is: red, green, blue, alpha.
   */
  RGBA_8888,

  /**
   * Each pixel is stored on 4 bytes. Each channel (RGB and alpha for translucency) is stored with 8
   * bits of precision (256 possible values). The channel order is: blue, green, red, alpha.
   */
  BGRA_8888,

  /**
   * Each pixel is stored in YCbCr 4:2:0 semi-planar format, also known as NV12. Commonly used for
   * video buffers, with a full-resolution Y plane and interleaved CbCr plane. Each pixel uses 1.5
   * bytes of memory.
   */
  YCBCR_420_SP
};

/**
 * Describes the properties of a hardware buffer.
 */
struct HardwareBufferInfo {
  /**
   * The width of the hardware buffer in pixels.
   */
  int width = 0;

  /**
   * The height of the hardware buffer in pixels.
   */
  int height = 0;

  /**
   * The pixel format of the hardware buffer.
   */
  HardwareBufferFormat format = HardwareBufferFormat::Unknown;

  /**
   * The number of bytes per row of the hardware buffer.
   */
  size_t rowBytes = 0;
};

/**
 * Returns true if the current platform has hardware buffer support. Otherwise, returns false.
 */
bool HardwareBufferAvailable();

/**
 * Returns true if the given hardware buffer object is valid and can be bind to a texture.
 * Otherwise, returns false.
 */
bool HardwareBufferCheck(HardwareBufferRef buffer);

/**
 * Allocates a hardware buffer for a given size and pixel format (alphaOnly). Returns nullptr if
 * allocation fails. The returned buffer has a reference count of 1, and the caller must call
 * HardwareBufferRelease() when finished with it.
 */
HardwareBufferRef HardwareBufferAllocate(int width, int height, bool alphaOnly = false);

/**
 * Retains a reference on the given hardware buffer object. This prevents the object from being
 * deleted until the last reference is removed.
 */
HardwareBufferRef HardwareBufferRetain(HardwareBufferRef buffer);

/**
 * Removes a reference that was previously acquired with HardwareBufferRetain().
 */
void HardwareBufferRelease(HardwareBufferRef buffer);

/**
 * Locks and returns the base address of the hardware buffer. Returns nullptr if the lock fails for
 * any reason and leave the buffer unchanged. The caller must call HardwareBufferUnlock() when
 * finished with the buffer.
 */
void* HardwareBufferLock(HardwareBufferRef buffer);

/**
 * Unlocks the base address of the hardware buffer. Call this to balance a successful call to
 * HardwareBufferLock().
 */
void HardwareBufferUnlock(HardwareBufferRef buffer);

/**
 * Returns a HardwareBufferInfo containing the width, height, format, and row bytes of the specified
 * hardware buffer. If the buffer is nullptr or unrecognized, returns an empty HardwareBufferInfo
 * with width and height set to zero.
 */
HardwareBufferInfo HardwareBufferGetInfo(HardwareBufferRef buffer);

}  // namespace tgfx

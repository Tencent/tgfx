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

#include <jni.h>
#include "tgfx/platform/ImageReader.h"

namespace tgfx {
/**
 * The SurfaceTextureReader class allows direct access to image data rendered into a SurfaceTexture
 * object on the Android platform. Notifications, acquisitions, and buffer state/timestamp queries
 * are synchronized. Texture reads must use the owning Context. Serialize colorSpace() queries and
 * use of its returned reference with texture reads, which can update the source color space.
 */
class SurfaceTextureReader : public ImageReader {
 public:
  /**
   * Creates a new SurfaceTextureReader with the specified image size and listener. Returns nullptr
   * if either dimension is non-positive, the listener is null, or native source initialization
   * fails. This reader is unavailable in builds using the Vulkan backend.
   * @param width The positive width of generated ImageBuffers in pixels.
   * @param height The positive height of generated ImageBuffers in pixels.
   * @param listener A java object that implements the SurfaceTexture.OnFrameAvailableListener
   * interface. The implementation should make a native call to the notifyFrameAvailable() of the
   * returned SurfaceTextureReader.
   */
  static std::shared_ptr<SurfaceTextureReader> Make(int width, int height, jobject listener);

  /**
   * Creates a new Java Surface object connected to the underlying SurfaceTexture. Returns nullptr
   * if creation fails. Each successful call returns a fresh Surface as a JNI local reference in the
   * caller's current local frame. The caller must call Surface.release() when done with the object.
   */
  jobject createInputSurface() const;

  /**
   * Marks source content as available for a subsequent texture read. This method should be invoked
   * by the listener provided when creating the reader; it does not perform a read or identify a
   * particular frame. Texture generation does not wait for this notification. Without new content,
   * a read can retain known-valid previous pixels. If neither new nor valid retained content is
   * available, the buffer becomes terminal Expired and cannot be revived by a later notification.
   * Acquire a new buffer for another attempt, normally after receiving the notification.
   */
  void notifyFrameAvailable();

 private:
  explicit SurfaceTextureReader(std::shared_ptr<ImageStream> stream);
};
}  // namespace tgfx

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
 * object on the Android platform. Frame notifications may arrive on another thread. Texture reads
 * must run on the owning GL context, and color-space access must be serialized with texture reads.
 */
class SurfaceTextureReader : public ImageReader {
 public:
  /**
   * Creates a new SurfaceTextureReader with the specified image size and listener. Returns nullptr
   * if either dimension is not positive, the listener is null, or initialization fails.
   * @param width The positive width of generated ImageBuffers.
   * @param height The positive height of generated ImageBuffers.
   * @param listener A java object that implements the SurfaceTexture.OnFrameAvailableListener
   * interface. The implementation should make a native call to the notifyFrameAvailable() of the
   * returned SurfaceTextureReader.
   */
  static std::shared_ptr<SurfaceTextureReader> Make(int width, int height, jobject listener);

  /**
   * Creates a new Java Surface object connected to the underlying SurfaceTexture. Each call returns
   * a fresh Surface as a JNI local reference in the caller's current local-reference frame.
   * @return The new Surface, or nullptr if creation fails. The caller must call Surface.release()
   * and delete the local reference when done.
   */
  jobject createInputSurface() const;

  /**
   * Records a frame-availability hint and wakes a pending texture read. Notifications may be
   * coalesced and do not count queued frames or guarantee a requested timestamp. The listener
   * provided when creating the reader should invoke this method, possibly on another thread.
   * A texture read waits for up to one second; individual native calls are not time bounded.
   */
  void notifyFrameAvailable();

 private:
  explicit SurfaceTextureReader(std::shared_ptr<ImageStream> stream);
};
}  // namespace tgfx

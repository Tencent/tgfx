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

#include <mutex>
#include "platform/ImageStream.h"
#include "tgfx/core/Size.h"
#include "tgfx/platform/android/Global.h"

namespace tgfx {
class Texture;

/**
 * The SurfaceTexture class allows direct access to image data rendered into the Java Surface object
 * on the android platform. It is typically used with the ImageReader class.
 */
class SurfaceTexture : public ImageStream {
 public:
  /**
   * Creates a source with the specified fixed image size and listener. Returns nullptr if either
   * dimension is non-positive, the listener is null, or JNI source/listener initialization fails.
   * @param width The positive width of source images in pixels.
   * @param height The positive height of source images in pixels.
   * @param listener A Java SurfaceTexture.OnFrameAvailableListener. Its callback must forward
   * notifications to notifyFrameAvailable() on the returned source, normally through
   * the owning SurfaceTextureReader.
   */
  static std::shared_ptr<SurfaceTexture> Make(int width, int height, jobject listener);

  ~SurfaceTexture() override;

  /**
   * Creates a Java Surface connected to this source, or returns nullptr if creation fails. The
   * returned JNI local reference belongs to the caller's local frame. Call Surface.release() when
   * done with the Surface object.
   */
  jobject createInputSurface() const;

  /**
   * Marks source content as available for a subsequent texture read without performing a read or
   * identifying a particular ImageBuffer. Texture generation does not wait for this notification,
   * and a later notification cannot revive an Expired buffer. Called by the creation listener.
   */
  void notifyFrameAvailable();

  const std::shared_ptr<ColorSpace>& colorSpace() const override {
    return _colorSpace;
  }

 protected:
  TextureReadResult onReadTexture(Context* context, bool mipmapped,
                                  std::shared_ptr<TextureView> texture) override;

 private:
  std::mutex locker = {};
  Global<jobject> surfaceTexture;
  bool frameAvailable = false;
  bool textureSizeResolved = false;
  int64_t lastLatchedTimestamp = -1;
  ISize textureSize = {};
  std::shared_ptr<ColorSpace> _colorSpace = nullptr;

  bool latchTexture(JNIEnv* env);

  int64_t currentTimestamp(JNIEnv* env);

  static void JNIInit(JNIEnv* env);

  SurfaceTexture(int width, int height, JNIEnv* env, jobject surfaceTexture);

  unsigned makeExternalOESTexture(Context* context);

  void releaseExternalOESTexture(Context* context, unsigned textureID);

  struct FrameUpdateResult {
    ReadOutcome outcome = ReadOutcome::Invalidated;
    int64_t timestamp = -1;
    ISize textureSize = {};
  };

  FrameUpdateResult updateTexImage();

  void resolveTextureSize(JNIEnv* env);

  friend class JNIInit;
};
}  // namespace tgfx

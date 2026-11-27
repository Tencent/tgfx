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

#include <condition_variable>
#include "platform/ImageStream.h"
#include "tgfx/platform/android/Global.h"

namespace tgfx {
class GPUTexture;

/**
 * The SurfaceTexture class allows direct access to image data rendered into the Java Surface object
 * on the android platform. It is typically used with the ImageReader class.
 */
class SurfaceTexture : public ImageStream {
 public:
  /**
   * Creates a SurfaceTexture with positive dimensions and a Java OnFrameAvailableListener that
   * forwards notifications to notifyFrameAvailable(). Returns nullptr for invalid arguments or
   * initialization failure.
   */
  static std::shared_ptr<SurfaceTexture> Make(int width, int height, jobject listener);

  ~SurfaceTexture() override;

  /**
   * Creates a new Java Surface object connected to this SurfaceTexture. Each call returns a fresh
   * Surface in the caller's JNI local-reference frame. The caller must release the Surface and
   * delete its local reference when done. Returns nullptr if creation fails.
   */
  jobject createInputSurface() const;

  /**
   * Notifies that a new frame is available. Called by the listener passed in when creating it.
   */
  void notifyFrameAvailable();

 protected:
  ReadResult onReadTexture(Context* context, bool mipmapped,
                           std::shared_ptr<TextureView> textureView, int64_t timestamp) override;

 private:
  std::mutex locker = {};
  std::condition_variable condition = {};
  Global<jobject> surfaceTexture;
  bool frameAvailable = false;
  bool latchedValid = false;
  bool textureSizeResolved = false;
  int64_t latchedTimestamp = -1;
  ISize textureSize = {};

  static void JNIInit(JNIEnv* env);

  SurfaceTexture(int width, int height, JNIEnv* env, jobject surfaceTexture);

  unsigned makeExternalOESTexture(Context* context);

  void releaseExternalOESTexture(Context* context, unsigned textureID);

  struct FrameResult {
    ImageBufferStatus status = ImageBufferStatus::Failed;
    bool latched = false;
  };

  FrameResult updateTexImage(int64_t timestamp);

  bool latchTexImage(JNIEnv* env);

  friend class JNIInit;
};
}  // namespace tgfx

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

#include "platform/ImageStream.h"
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
   * Creates a new SurfaceTexture with the specified image size and listener. Returns nullptr
   * if the image size is zero or the listener is null.
   * @param width The width of generated ImageBuffers.
   * @param height The height of generated ImageBuffers.
   * @param listener A java object that implements the SurfaceTexture.OnFrameAvailableListener
   * interface. The implementation should make a native call to the notifyFrameAvailable() of the
   * returned SurfaceImageReader.
   */
  static std::shared_ptr<SurfaceTexture> Make(int width, int height, jobject listener);

  ~SurfaceTexture() override;

  /**
   * Returns the Surface object used as the input to the SurfaceTexture. The release() method of
   * the returned Surface will be called when the SurfaceTexture is released.
   */
  jobject getInputSurface() const;

  /**
   * Notifies the previously returned ImageBuffer is available for generating textures. The method
   * should only be called by the listener passed in when creating the reader.
   */
  void notifyFrameAvailable();

  std::shared_ptr<ColorSpace> colorSpace() const override {
    return _colorSpace;
  }

 protected:
  std::shared_ptr<TextureView> onMakeTexture(Context* context, bool mipmapped) override;

  bool onUpdateTexture(std::shared_ptr<TextureView> textureView) override;

 private:
  std::mutex locker = {};
  std::condition_variable condition = {};
  Global<jobject> surface;
  Global<jobject> surfaceTexture;
  bool frameAvailable = false;
  std::shared_ptr<ColorSpace> _colorSpace = ColorSpace::SRGB();

  static void JNIInit(JNIEnv* env);

  SurfaceTexture(int width, int height, JNIEnv* env, jobject surfaceTexture);

  unsigned makeExternalOESTexture(Context* context);

  ISize updateTexImage();

  friend class JNIInit;
};
}  // namespace tgfx

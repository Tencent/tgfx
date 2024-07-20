/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "SurfaceTexture.h"
#include <chrono>
#include "GLExternalOESTexture.h"
#include "opengl/GLSampler.h"
#include "tgfx/opengl/GLDefines.h"
#include "utils/Log.h"

namespace tgfx {
static std::mutex threadLocker = {};

std::shared_ptr<SurfaceTexture> SurfaceTexture::Make(int width, int height) {
  if (width <= 0 || height <= 0) {
    return nullptr;
  }
  return std::shared_ptr<SurfaceTexture>(new SurfaceTexture(width, height));
}

SurfaceTexture::SurfaceTexture(int width, int height)
    : _width(width), _height(height) {
    nativeImage = OH_NativeImage_Create(0, GL_TEXTURE_EXTERNAL_OES);
    nativeWindow = OH_NativeImage_AcquireNativeWindow(nativeImage);
}

SurfaceTexture::~SurfaceTexture() {
   if (nativeImage) {
        OH_NativeWindow_DestroyNativeWindow(nativeWindow);
        OH_NativeImage_Destroy(&nativeImage);
   }
}

OHNativeWindow* SurfaceTexture::getInputSurface() const {
  return nativeWindow;
}

void SurfaceTexture::notifyFrameAvailable() {
  // Note: If there is a pending frame available already, SurfaceTexture will not dispatch any new
  // frame-available event until you have called the SurfaceTexture.updateTexImage().
  std::lock_guard<std::mutex> autoLock(locker);
  frameAvailable = true;
  condition.notify_all();
}

static ISize ComputeTextureSize(float matrix[16], int width, int height) {
  Size size = {static_cast<float>(width), static_cast<float>(height)};
  auto scaleX = fabsf(matrix[0]);
  if (scaleX > 0) {
    size.width = size.width / (scaleX + matrix[12] * 2);
  }
  float scaleY = fabsf(matrix[5]);
  if (scaleY > 0) {
    size.height = size.height / (scaleY + (matrix[13] - scaleY) * 2);
  }
  return size.toRound();
}

std::shared_ptr<Texture> SurfaceTexture::onMakeTexture(Context* context, bool) {
  auto texture = makeTexture(context);
  if (texture != nullptr) {
    onUpdateTexture(texture, Rect::MakeWH(_width, _height));
  }
  return texture;
}

bool SurfaceTexture::onUpdateTexture(std::shared_ptr<Texture> texture, const Rect&) {
  std::unique_lock<std::mutex> autoLock(locker);
  if (!frameAvailable) {
    static const auto TIMEOUT = std::chrono::seconds(1);
    auto status = condition.wait_for(autoLock, TIMEOUT);
    if (status == std::cv_status::timeout) {
      LOGE("NativeImageReader::onUpdateTexture(): timeout when waiting for the frame available!");
      return false;
    }
  }
  frameAvailable = false;
  int ret = OH_NativeImage_UpdateSurfaceImage(nativeImage);
  if (ret != 0) {
    LOGE("NativeImageReader::onUpdateTexture(): failed to updateTexImage!");
    return false;
  }
  float matrix[16];
  ret = OH_NativeImage_GetTransformMatrix(nativeImage, matrix);
  if (ret != 0) {
    LOGE("NativeImageReader::onUpdateTexture(): failed to getTransformMatrix!");
    return false;
  }  
  auto textureSize = ComputeTextureSize(matrix, width(), height());
  std::static_pointer_cast<GLExternalOESTexture>(texture)->updateTextureSize(textureSize.width,
                                                                             textureSize.height);
  return true;
}

std::shared_ptr<Texture> SurfaceTexture::makeTexture(Context* context) {
  std::lock_guard<std::mutex> autoLock(locker);
  auto texture = GLExternalOESTexture::Make(context, width(), height());
  if (texture == nullptr) {
    return nullptr;
  }
  auto sampler = static_cast<const GLSampler*>(texture->getSampler());
  OH_NativeImage_AttachContext(nativeImage, sampler->id);
  if (OH_NativeImage_AttachContext(nativeImage, sampler->id) != 0) {
    texture = nullptr;
    LOGE("NativeImageReader::makeTexture(): failed to attached to a SurfaceTexture!");
    return nullptr;
  }
  return texture;
}
}  // namespace tgfx

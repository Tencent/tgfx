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

#include "platform/web/VideoElement.h"
#include "gpu/DefaultTexture.h"
#include "gpu/opengl/GLTextureSampler.h"

namespace tgfx {
using namespace emscripten;

static constexpr int ANDROID_MINIPROGRAM_ALIGNMENT = 16;

std::shared_ptr<VideoElement> VideoElement::MakeFrom(val video, int width, int height) {
  if (video == val::null() || width < 1 || height < 1) {
    return nullptr;
  }
  return std::shared_ptr<VideoElement>(new VideoElement(video, width, height));
}

VideoElement::VideoElement(emscripten::val video, int width, int height)
    : ImageStream(width, height), source(video) {
}

void VideoElement::markFrameChanged(emscripten::val promise) {
  currentPromise = promise;
#ifndef TGFX_USE_ASYNC_PROMISE
  if (currentPromise != val::null()) {
    currentPromise.await();
  }
#endif
}

std::shared_ptr<Texture> VideoElement::onMakeTexture(Context* context, bool mipmapped) {
  static auto isAndroidMiniprogram =
      val::module_property("tgfx").call<bool>("isAndroidMiniprogram");
  auto textureWidth = width();
  auto textureHeight = height();
  if (isAndroidMiniprogram) {
    // https://stackoverflow.com/questions/28291204/something-about-stagefright-codec-input-format-in-android
    // Video decoder will align to multiples of 16 on the Android WeChat mini-program.
    if (textureWidth % ANDROID_MINIPROGRAM_ALIGNMENT != 0) {
      textureWidth +=
          ANDROID_MINIPROGRAM_ALIGNMENT - (textureWidth % ANDROID_MINIPROGRAM_ALIGNMENT);
    }
    if (textureHeight % ANDROID_MINIPROGRAM_ALIGNMENT != 0) {
      textureHeight +=
          ANDROID_MINIPROGRAM_ALIGNMENT - (textureHeight % ANDROID_MINIPROGRAM_ALIGNMENT);
    }
  }
  auto texture =
      Texture::MakeFormat(context, textureWidth, textureHeight, PixelFormat::RGBA_8888, mipmapped);
  if (texture != nullptr) {
    onUpdateTexture(texture);
  }
  return texture;
}

bool VideoElement::onUpdateTexture(std::shared_ptr<Texture> texture) {
#ifdef TGFX_USE_ASYNC_PROMISE
  if (currentPromise != val::null()) {
    currentPromise.await();
  }
#endif
  auto sampler = static_cast<GLTextureSampler*>(texture->getSampler());
  val::module_property("tgfx").call<void>("uploadToTexture", emscripten::val::module_property("GL"),
                                          source, sampler->id(), false);
  return true;
}
}  // namespace tgfx

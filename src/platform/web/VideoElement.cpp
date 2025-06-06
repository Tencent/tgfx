/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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
#include "GLVideoTexture.h"

namespace tgfx {
using namespace emscripten;

std::shared_ptr<VideoElement> VideoElement::MakeFrom(val video, int width, int height) {
  if (video == val::null() || width < 1 || height < 1) {
    return nullptr;
  }
  return std::shared_ptr<VideoElement>(new VideoElement(video, width, height));
}

VideoElement::VideoElement(emscripten::val video, int width, int height)
    : WebImageStream(video, width, height, false) {
}

void VideoElement::markFrameChanged(emscripten::val promise) {
  currentPromise = promise;
  markContentDirty(Rect::MakeWH(width(), height()));
#ifndef TGFX_USE_ASYNC_PROMISE
  if (currentPromise != val::null()) {
    currentPromise.await();
  }
#endif
}

std::shared_ptr<Texture> VideoElement::onMakeTexture(Context* context, bool mipmapped) {
  auto texture = GLVideoTexture::Make(context, width(), height(), mipmapped);
  if (texture != nullptr) {
    onUpdateTexture(texture, Rect::MakeWH(width(), height()));
  }
  return texture;
}

bool VideoElement::onUpdateTexture(std::shared_ptr<Texture> texture, const Rect& bounds) {
#ifdef TGFX_USE_ASYNC_PROMISE
  if (currentPromise != val::null()) {
    currentPromise.await();
  }
#endif
  return WebImageStream::onUpdateTexture(texture, bounds);
}

}  // namespace tgfx
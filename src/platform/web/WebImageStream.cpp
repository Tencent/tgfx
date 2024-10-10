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

#include "platform/web/WebImageStream.h"
#include "gpu/Gpu.h"
#include "gpu/Texture.h"
#include "gpu/opengl/GLSampler.h"

namespace tgfx {
using namespace emscripten;

std::shared_ptr<WebImageStream> WebImageStream::MakeFrom(val source, int width, int height,
                                                         bool alphaOnly) {
  if (source == val::null() || width < 1 || height < 1) {
    return nullptr;
  }
  return std::shared_ptr<WebImageStream>(new WebImageStream(source, width, height, alphaOnly));
}

WebImageStream::WebImageStream(emscripten::val source, int width, int height, bool alphaOnly)
    : source(source), _width(width), _height(height), alphaOnly(alphaOnly) {
}

std::shared_ptr<Texture> WebImageStream::onMakeTexture(Context* context, bool mipmapped) {
  std::shared_ptr<Texture> texture = nullptr;
  if (alphaOnly) {
    texture = Texture::MakeAlpha(context, width(), height(), mipmapped);
  } else {
    texture = Texture::MakeRGBA(context, width(), height(), mipmapped);
  }
  if (texture != nullptr) {
    onUpdateTexture(texture, Rect::MakeWH(_width, _height));
  }
  return texture;
}

bool WebImageStream::onUpdateTexture(std::shared_ptr<Texture> texture, const Rect&) {
  auto glSampler = static_cast<const GLSampler*>(texture->getSampler());
  val::module_property("tgfx").call<void>("uploadToTexture", emscripten::val::module_property("GL"),
                                          source, glSampler->id, alphaOnly);
  if (glSampler->hasMipmaps()) {
    auto gpu = texture->getContext()->gpu();
    gpu->regenerateMipmapLevels(glSampler);
  }
  return true;
}

}  // namespace tgfx
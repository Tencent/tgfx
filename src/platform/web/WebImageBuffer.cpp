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

#include "WebImageBuffer.h"
#include "gpu/opengl/GLTexture.h"
#include "gpu/resources/TextureView.h"
#include "tgfx/core/ImageCodec.h"

using namespace emscripten;

namespace tgfx {
std::shared_ptr<WebImageBuffer> WebImageBuffer::MakeFrom(emscripten::val nativeImage) {
  if (!nativeImage.as<bool>()) {
    return nullptr;
  }
  auto size = val::module_property("tgfx").call<val>("getSourceSize", nativeImage);
  auto width = size["width"].as<int>();
  auto height = size["height"].as<int>();
  if (width < 1 || height < 1) {
    return nullptr;
  }
  return std::shared_ptr<WebImageBuffer>(new WebImageBuffer(width, height, nativeImage));
}

std::shared_ptr<WebImageBuffer> WebImageBuffer::MakeAdopted(emscripten::val nativeImage,
                                                            bool alphaOnly) {
  if (!nativeImage.as<bool>()) {
    return nullptr;
  }
  auto size = val::module_property("tgfx").call<val>("getSourceSize", nativeImage);
  auto width = size["width"].as<int>();
  auto height = size["height"].as<int>();
  if (width < 1 || height < 1) {
    return nullptr;
  }
  auto imageBuffer =
      std::shared_ptr<WebImageBuffer>(new WebImageBuffer(width, height, nativeImage, alphaOnly));
  imageBuffer->adopted = true;
  return imageBuffer;
}

WebImageBuffer::WebImageBuffer(int width, int height, emscripten::val nativeImage, bool alphaOnly)
    : _width(width), _height(height), _alphaOnly(alphaOnly), nativeImage(nativeImage) {
}

WebImageBuffer::~WebImageBuffer() {
  if (adopted) {
    val::module_property("tgfx").call<void>("releaseNativeImage", nativeImage);
  }
}

bool WebImageBuffer::uploadToTexture(std::shared_ptr<Texture> texture, int offsetX,
                                     int offsetY) const {
  if (texture == nullptr || !nativeImage.as<bool>()) {
    return false;
  }
  auto glTexture = std::static_pointer_cast<GLTexture>(texture);
  val::module_property("tgfx").call<void>("uploadToTextureRegion", val::module_property("GL"),
                                          nativeImage, glTexture->textureID(), offsetX, offsetY,
                                          _alphaOnly);
  return true;
}

std::shared_ptr<TextureView> WebImageBuffer::onMakeTexture(Context* context, bool) const {
  auto textureView = TextureView::MakeRGBA(context, width(), height(), nullptr, 0);
  if (textureView == nullptr) {
    return nullptr;
  }
  auto glTexture = std::static_pointer_cast<GLTexture>(textureView->getTexture());
  val::module_property("tgfx").call<void>("uploadToTexture", emscripten::val::module_property("GL"),
                                          getImage(), glTexture->textureID(), _alphaOnly);
  return textureView;
}

emscripten::val WebImageBuffer::getImage() const {
  return nativeImage;
}
}  // namespace tgfx

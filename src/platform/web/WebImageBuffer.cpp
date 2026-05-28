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
#include "gpu/resources/TextureView.h"
#include "tgfx/core/Buffer.h"
#include "tgfx/core/ImageCodec.h"
#include "tgfx/gpu/CommandQueue.h"
#include "tgfx/gpu/GPU.h"

using namespace emscripten;

namespace tgfx {

static std::shared_ptr<Data> CopyPixelsFromNativeImage(const val& nativeImage, int width,
                                                       int height) {
  auto data = val::module_property("tgfx").call<val>("readImagePixels",
                                                     val::module_property("module"), nativeImage,
                                                     width, height);
  if (!data.as<bool>()) {
    return nullptr;
  }
  auto length = data["length"].as<size_t>();
  if (length == 0) {
    return nullptr;
  }
  Buffer buffer(length);
  if (buffer.isEmpty()) {
    return nullptr;
  }
  auto memory = val::module_property("HEAPU8")["buffer"];
  auto memoryView =
      val::global("Uint8Array").new_(memory, reinterpret_cast<uintptr_t>(buffer.data()), length);
  memoryView.call<void>("set", data);
  return buffer.release();
}

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

bool WebImageBuffer::uploadToTexture(std::shared_ptr<Texture> texture, CommandQueue* queue,
                                     int offsetX, int offsetY) const {
  if (texture == nullptr || queue == nullptr || !nativeImage.as<bool>()) {
    return false;
  }
  auto pixelData = CopyPixelsFromNativeImage(getImage(), _width, _height);
  if (pixelData == nullptr) {
    return false;
  }
  auto rect = Rect::MakeXYWH(offsetX, offsetY, _width, _height);
  if (_alphaOnly) {
    // Canvas 2D always returns RGBA data. Extract the alpha channel for R8 texture upload.
    auto pixelCount = static_cast<size_t>(_width) * static_cast<size_t>(_height);
    Buffer alphaBuffer(pixelCount);
    auto src = static_cast<const uint8_t*>(pixelData->data());
    auto dst = static_cast<uint8_t*>(alphaBuffer.data());
    for (size_t i = 0; i < pixelCount; i++) {
      dst[i] = src[i * 4 + 3];
    }
    auto rowBytes = static_cast<size_t>(_width);
    queue->writeTexture(texture, rect, alphaBuffer.data(), rowBytes);
  } else {
    auto rowBytes = static_cast<size_t>(_width) * 4;
    queue->writeTexture(texture, rect, pixelData->data(), rowBytes);
  }
  return true;
}

std::shared_ptr<TextureView> WebImageBuffer::onMakeTexture(Context* context, bool) const {
  auto pixelData = CopyPixelsFromNativeImage(getImage(), _width, _height);
  if (pixelData == nullptr) {
    return nullptr;
  }
  auto rowBytes = static_cast<size_t>(_width) * 4;
  if (_alphaOnly) {
    return TextureView::MakeAlpha(context, _width, _height, pixelData->data(), rowBytes);
  }
  return TextureView::MakeRGBA(context, _width, _height, pixelData->data(), rowBytes);
}

emscripten::val WebImageBuffer::getImage() const {
  return nativeImage;
}
}  // namespace tgfx

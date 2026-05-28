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
#include "tgfx/core/Pixmap.h"
#include "tgfx/gpu/CommandQueue.h"
#include "tgfx/gpu/GPU.h"

using namespace emscripten;

namespace tgfx {

static std::shared_ptr<Data> CopyPixelsFromNativeImage(const val& nativeImage, int width,
                                                       int height) {
  auto data = val::module_property("tgfx").call<val>(
      "readImagePixels", val::module_property("module"), nativeImage, width, height);
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
  // Canvas 2D getImageData returns unpremultiplied RGBA. Convert to the format expected by the
  // texture (premultiplied, matching the native AtlasUploadTask behavior).
  auto srcInfo = ImageInfo::Make(_width, _height, ColorType::RGBA_8888, AlphaType::Unpremultiplied,
                                 static_cast<size_t>(_width) * 4);
  if (_alphaOnly) {
    // Target is R8 (alpha-only): convert RGBA unpremul to ALPHA_8.
    auto dstInfo = ImageInfo::Make(_width, _height, ColorType::ALPHA_8, AlphaType::Premultiplied,
                                   static_cast<size_t>(_width));
    Buffer dstBuffer(dstInfo.byteSize());
    Pixmap srcPixmap(srcInfo, pixelData->data());
    srcPixmap.readPixels(dstInfo, dstBuffer.data());
    auto rect = Rect::MakeXYWH(offsetX, offsetY, _width, _height);
    queue->writeTexture(texture, rect, dstBuffer.data(), static_cast<size_t>(_width));
  } else {
    // Target is RGBA8: convert unpremultiplied to premultiplied.
    auto dstInfo = ImageInfo::Make(_width, _height, ColorType::RGBA_8888, AlphaType::Premultiplied,
                                   static_cast<size_t>(_width) * 4);
    Buffer dstBuffer(dstInfo.byteSize());
    Pixmap srcPixmap(srcInfo, pixelData->data());
    srcPixmap.readPixels(dstInfo, dstBuffer.data());
    auto rect = Rect::MakeXYWH(offsetX, offsetY, _width, _height);
    queue->writeTexture(texture, rect, dstBuffer.data(), static_cast<size_t>(_width) * 4);
  }
  return true;
}

std::shared_ptr<TextureView> WebImageBuffer::onMakeTexture(Context* context, bool mipmapped) const {
  auto pixelData = CopyPixelsFromNativeImage(getImage(), _width, _height);
  if (pixelData == nullptr) {
    return nullptr;
  }
  // Canvas 2D getImageData returns unpremultiplied RGBA. Convert to premultiplied before uploading
  // to match the native ImageCodec behavior (all GPU texture data must be premultiplied).
  auto srcInfo = ImageInfo::Make(_width, _height, ColorType::RGBA_8888, AlphaType::Unpremultiplied,
                                 static_cast<size_t>(_width) * 4);
  if (_alphaOnly) {
    auto dstInfo = ImageInfo::Make(_width, _height, ColorType::ALPHA_8, AlphaType::Premultiplied,
                                   static_cast<size_t>(_width));
    Buffer dstBuffer(dstInfo.byteSize());
    Pixmap srcPixmap(srcInfo, pixelData->data());
    srcPixmap.readPixels(dstInfo, dstBuffer.data());
    return TextureView::MakeAlpha(context, _width, _height, dstBuffer.data(),
                                  static_cast<size_t>(_width), mipmapped);
  }
  auto dstInfo = ImageInfo::Make(_width, _height, ColorType::RGBA_8888, AlphaType::Premultiplied,
                                 static_cast<size_t>(_width) * 4);
  Buffer dstBuffer(dstInfo.byteSize());
  Pixmap srcPixmap(srcInfo, pixelData->data());
  srcPixmap.readPixels(dstInfo, dstBuffer.data());
  return TextureView::MakeRGBA(context, _width, _height, dstBuffer.data(),
                               static_cast<size_t>(_width) * 4, mipmapped);
}

emscripten::val WebImageBuffer::getImage() const {
  return nativeImage;
}
}  // namespace tgfx

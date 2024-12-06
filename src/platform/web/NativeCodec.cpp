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

#include "NativeCodec.h"
#include <atomic>
#include "WebImageBuffer.h"
#include "WebImageInfo.h"
#include "tgfx/core/Buffer.h"
#include "tgfx/core/Stream.h"
#include "tgfx/platform/web/WebCodec.h"

using namespace emscripten;

namespace tgfx {

std::shared_ptr<Data> CopyDataFromUint8Array(const val& emscriptenData) {
  if (!emscriptenData.as<bool>()) {
    return nullptr;
  }
  auto length = emscriptenData["length"].as<size_t>();
  if (length == 0) {
    return nullptr;
  }
  Buffer imageBuffer(length);
  if (imageBuffer.isEmpty()) {
    return nullptr;
  }
  auto memory = val::module_property("HEAPU8")["buffer"];
  auto memoryView = val::global("Uint8Array")
                        .new_(memory, reinterpret_cast<uintptr_t>(imageBuffer.data()), length);
  memoryView.call<void>("set", emscriptenData);
  return imageBuffer.release();
}

std::shared_ptr<ImageCodec> ImageCodec::MakeNativeCodec(const std::string& filePath) {
  if (filePath.find("http://") == 0 || filePath.find("https://") == 0) {
    auto data = val::module_property("tgfx")
                    .call<val>("getBytesFromPath", val::module_property("module"), filePath)
                    .await();
    auto imageData = CopyDataFromUint8Array(data);
    if (imageData == nullptr) {
      return nullptr;
    }
    return ImageCodec::MakeNativeCodec(imageData);
  } else {
    auto imageStream = Stream::MakeFromFile(filePath);
    if (imageStream == nullptr || imageStream->size() <= 14) {
      return nullptr;
    }
    Buffer imageBuffer(imageStream->size());
    imageStream->read(imageBuffer.data(), imageStream->size());
    auto imageData = imageBuffer.release();
    return ImageCodec::MakeNativeCodec(imageData);
  }
}

std::shared_ptr<ImageCodec> ImageCodec::MakeNativeCodec(std::shared_ptr<Data> imageBytes) {
  auto imageSize = WebImageInfo::GetSize(imageBytes);
  if (imageSize.isEmpty()) {
    return nullptr;
  }
  return std::shared_ptr<ImageCodec>(
      new NativeCodec(imageSize.width, imageSize.height, std::move(imageBytes)));
}

std::shared_ptr<ImageCodec> ImageCodec::MakeFrom(NativeImageRef nativeImage) {
  if (!nativeImage.as<bool>()) {
    return nullptr;
  }
  auto size = val::module_property("tgfx").call<val>("getSourceSize", nativeImage);
  auto width = size["width"].as<int>();
  auto height = size["height"].as<int>();
  if (width < 1 || height < 1) {
    return nullptr;
  }
  return std::shared_ptr<NativeCodec>(new NativeCodec(width, height, std::move(nativeImage)));
}

NativeCodec::NativeCodec(int width, int height, std::shared_ptr<Data> imageBytes)
    : ImageCodec(width, height, Orientation::TopLeft), imageBytes(std::move(imageBytes)) {
}

NativeCodec::NativeCodec(int width, int height, emscripten::val nativeImage)
    : ImageCodec(width, height, Orientation::TopLeft), nativeImage(std::move(nativeImage)) {
}

bool NativeCodec::asyncSupport() const {
  // The native codec can be only used in the main thread.
  return false;
}

bool NativeCodec::readPixels(const ImageInfo& dstInfo, void* dstPixels) const {
  if (dstInfo.isEmpty() || dstPixels == nullptr) {
    return false;
  }
  auto image = nativeImage;
  if (!image.as<bool>()) {
    auto bytes =
        val(typed_memory_view(imageBytes->size(), static_cast<const uint8_t*>(imageBytes->data())));
    image = val::module_property("tgfx").call<val>("createImageFromBytes", bytes).await();
  }

  auto data = val::module_property("tgfx").call<val>(
      "readImagePixels", val::module_property("module"), image, dstInfo.width(), dstInfo.height());
  auto imageData = CopyDataFromUint8Array(data);
  if (imageData == nullptr) {
    return false;
  }
  auto info = ImageInfo::Make(width(), height(), ColorType::RGBA_8888, AlphaType::Unpremultiplied);
  Pixmap pixmap(info, imageData->data());
  auto result = pixmap.readPixels(dstInfo, dstPixels);
  return result;
}

std::shared_ptr<ImageBuffer> NativeCodec::onMakeBuffer(bool) const {
  auto image = nativeImage;
  bool usePromise = false;
  if (!image.as<bool>()) {
    auto bytes =
        val(typed_memory_view(imageBytes->size(), static_cast<const uint8_t*>(imageBytes->data())));
    image = val::module_property("tgfx").call<val>("createImageFromBytes", bytes);
    usePromise = WebCodec::AsyncSupport();
    if (!usePromise) {
      image = image.await();
    }
  }
  return std::shared_ptr<WebImageBuffer>(
      new WebImageBuffer(width(), height(), std::move(image), usePromise));
}
}  // namespace tgfx

/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "ReadPixelsFromCanvasImage.h"
#include "tgfx/core/Pixmap.h"

namespace tgfx {
bool ReadPixelsFromCanvasImage(emscripten::val canvasImageData, const ImageInfo& dstInfo,
                               void* dstPixels) {
  if (dstPixels == nullptr || dstInfo.isEmpty() || canvasImageData.isNull()) {
    return false;
  }
  auto length = canvasImageData["length"].as<size_t>();
  auto memory = emscripten::val::module_property("HEAPU8")["buffer"];
  if (dstInfo.colorType() == ColorType::RGBA_8888 && dstInfo.rowBytes() == dstInfo.minRowBytes()) {
    auto memoryView = emscripten::val::global("Uint8Array")
                          .new_(memory, reinterpret_cast<uintptr_t>(dstPixels), length);
    memoryView.call<void>("set", canvasImageData);

  } else {
    if (length == 0 || length % 4 != 0) {
      return false;
    }
    auto buffer = new (std::nothrow) int8_t[length];
    if (buffer == nullptr) {
      return false;
    }
    auto memoryView = emscripten::val::global("Uint8Array")
                          .new_(memory, reinterpret_cast<uintptr_t>(buffer), length);
    memoryView.call<void>("set", canvasImageData);
    auto srcInfo = ImageInfo::Make(dstInfo.width(), dstInfo.height(), ColorType::RGBA_8888,
                                   AlphaType::Premultiplied);
    Pixmap RGBAMap(srcInfo, buffer);
    RGBAMap.readPixels(dstInfo, dstPixels);
    delete[] buffer;
  }
  return true;
}
}  // namespace tgfx

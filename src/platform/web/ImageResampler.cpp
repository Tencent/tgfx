/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "platform/ImageResampler.h"
#include <tgfx/core/Buffer.h>
#include "CopyDataUtils.h"

using namespace emscripten;

namespace tgfx {
bool ImageResamper::Scale(const ImageInfo& srcInfo, const void* srcData, const ImageInfo& dstInfo,
                          void* dstData, FilterQuality quality) {
  if (srcInfo.isEmpty() || srcData == nullptr || dstInfo.isEmpty() || dstData == nullptr) {
    return false;
  }
  auto bytes = val(typed_memory_view(srcInfo.byteSize(), static_cast<const uint8_t*>(srcData)));
  auto data = val::module_property("tgfx").call<val>(
      "scaleImage", val::module_property("module"), bytes, srcInfo.width(), srcInfo.height(),
      dstInfo.width(), dstInfo.height(), static_cast<int>(quality));
  auto imageData = CopyDataFromUint8Array(data);
  if (imageData == nullptr) {
    return false;
  }
  return Pixmap(srcInfo, imageData->data()).readPixels(dstInfo, dstData);
}
}  // namespace tgfx
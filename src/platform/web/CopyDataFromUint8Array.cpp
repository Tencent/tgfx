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

#include "CopyDataFromUint8Array.h"
#include "tgfx/core/Buffer.h"

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
}  // namespace tgfx
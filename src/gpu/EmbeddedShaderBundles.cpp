/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "gpu/EmbeddedShaderBundles.h"

namespace tgfx {

struct BundleEntry {
  const uint8_t* data = nullptr;
  size_t size = 0;
};

// Backend enum: Unknown=0, OpenGL=1, Metal=2, Vulkan=3, WebGPU=4
static constexpr int BACKEND_COUNT = 5;
static_assert(static_cast<int>(Backend::WebGPU) < BACKEND_COUNT, "Backend enum exceeds array size");

static BundleEntry& GetEntry(Backend backend) {
  static BundleEntry entries[BACKEND_COUNT];
  auto index = static_cast<int>(backend);
  if (index < 0 || index >= BACKEND_COUNT) {
    static BundleEntry empty;
    return empty;
  }
  return entries[index];
}

void EmbeddedShaderBundles::Register(Backend backend, const uint8_t* data, size_t size) {
  auto& entry = GetEntry(backend);
  entry.data = data;
  entry.size = size;
}

std::pair<const uint8_t*, size_t> EmbeddedShaderBundles::GetBundle(Backend backend) {
  auto& entry = GetEntry(backend);
  if (entry.data != nullptr && entry.size > 0) {
    return {entry.data, entry.size};
  }
  return {nullptr, 0};
}

}  // namespace tgfx

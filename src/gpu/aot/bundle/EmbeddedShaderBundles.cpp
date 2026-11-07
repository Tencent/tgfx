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

#include "gpu/aot/bundle/EmbeddedShaderBundles.h"
#include <mutex>

namespace tgfx {

struct BundleEntry {
  const uint8_t* data = nullptr;
  size_t size = 0;
};

// Backend enum: Unknown=0, OpenGL=1, Metal=2, Vulkan=3, WebGPU=4, D3D12=5
static constexpr size_t BackendCount = static_cast<size_t>(Backend::D3D12) + 1;
// A new backend must be reviewed here: the table is keyed by the enum value, so a slot the table
// does not carry would silently drop that backend's embedded bundle.
static_assert(BackendCount == 6, "a new backend needs its embedded bundle slot reviewed");

static BundleEntry* FindEntry(Backend backend) {
  static BundleEntry entries[BackendCount];
  auto index = static_cast<size_t>(static_cast<int>(backend));
  return index < BackendCount ? &entries[index] : nullptr;
}

void EmbeddedShaderBundles::Register(Backend backend, const uint8_t* data, size_t size) {
  auto* entry = FindEntry(backend);
  if (entry == nullptr) {
    return;
  }
  entry->data = data;
  entry->size = size;
}

std::pair<const uint8_t*, size_t> EmbeddedShaderBundles::GetBundle(Backend backend) {
  static std::once_flag registerOnce;
  std::call_once(registerOnce, [] {
#if defined(TGFX_EMBED_SHADER_BUNDLES)
    embedded::RegisterAllEmbeddedBundles();
#endif
  });
  auto* entry = FindEntry(backend);
  if (entry != nullptr && entry->data != nullptr && entry->size > 0) {
    return {entry->data, entry->size};
  }
  return {nullptr, 0};
}

}  // namespace tgfx

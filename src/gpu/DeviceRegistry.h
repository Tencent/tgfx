/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "as IS" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace tgfx {

// The native identity of a Device. GPU backends are mutually exclusive in a build
// (CMakeLists.txt if/elseif chains), so exactly one branch below is compiled in. The key is kept
// out of the public Device.h on purpose: storing it as a member would force Device.h to include
// this internal header.
struct DeviceKey {
#if defined(TGFX_USE_METAL)
  const void* device = nullptr;
#elif defined(TGFX_USE_VULKAN)
  const void* instance = nullptr;
  const void* physicalDevice = nullptr;
  const void* device = nullptr;
  const void* queue = nullptr;
  uint32_t queueFamilyIndex = 0;
#else  // OpenGL / WebGL
  const void* nativeHandle = nullptr;
#endif

  bool operator==(const DeviceKey& other) const {
#if defined(TGFX_USE_METAL)
    return device == other.device;
#elif defined(TGFX_USE_VULKAN)
    return instance == other.instance && physicalDevice == other.physicalDevice &&
           device == other.device && queue == other.queue &&
           queueFamilyIndex == other.queueFamilyIndex;
#else
    return nativeHandle == other.nativeHandle;
#endif
  }
};

struct DeviceKeyHash {
  size_t operator()(const DeviceKey& key) const {
#if defined(TGFX_USE_METAL)
    return std::hash<const void*>()(key.device);
#elif defined(TGFX_USE_VULKAN)
    size_t seed = std::hash<const void*>()(key.instance);
    seed ^= std::hash<const void*>()(key.physicalDevice) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    seed ^= std::hash<const void*>()(key.device) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    seed ^= std::hash<const void*>()(key.queue) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    seed ^= std::hash<uint32_t>()(key.queueFamilyIndex) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    return seed;
#else
    return std::hash<const void*>()(key.nativeHandle);
#endif
  }
};

}  // namespace tgfx

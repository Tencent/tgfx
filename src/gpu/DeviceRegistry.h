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
#include <functional>

namespace tgfx {

// The native identity of a Device. GPU backends are mutually exclusive in a build
// (CMakeLists.txt if/elseif chains), so exactly one branch below is compiled in. The key is kept
// out of the public Device.h on purpose: storing it as a member would force Device.h to include
// this internal header.
struct DeviceKey {
#if defined(TGFX_USE_METAL)
  const void* device = nullptr;
#else  // OpenGL / WebGL (D3D12 / WebGPU builds also fall here but never register)
  const void* nativeHandle = nullptr;
#endif

  bool operator==(const DeviceKey& other) const {
#if defined(TGFX_USE_METAL)
    return device == other.device;
#else
    return nativeHandle == other.nativeHandle;
#endif
  }
};

struct DeviceKeyHash {
  size_t operator()(const DeviceKey& key) const {
#if defined(TGFX_USE_METAL)
    return std::hash<const void*>()(key.device);
#else
    return std::hash<const void*>()(key.nativeHandle);
#endif
  }
};

}  // namespace tgfx

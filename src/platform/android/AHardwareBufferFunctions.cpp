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

#include "AHardwareBufferFunctions.h"
#include <dlfcn.h>
#include <sys/system_properties.h>
#include <cstdlib>
#include <string>

namespace tgfx {
template <typename T>
static void LoadSymbol(T*& function, const char* symbol) {
  function = (T*)dlsym(RTLD_DEFAULT, symbol);
}

const AHardwareBufferFunctions* AHardwareBufferFunctions::Get() {
  static AHardwareBufferFunctions functions = *new AHardwareBufferFunctions();
  return &functions;
}

AHardwareBufferFunctions::AHardwareBufferFunctions() {
  char sdk[PROP_VALUE_MAX] = "0";
  __system_property_get("ro.build.version.sdk", sdk);
  auto version = std::stoi(sdk);
  if (version >= 26) {
    LoadSymbol(allocate, "AHardwareBuffer_allocate");
    LoadSymbol(acquire, "AHardwareBuffer_acquire");
    LoadSymbol(release, "AHardwareBuffer_release");
    LoadSymbol(describe, "AHardwareBuffer_describe");
    LoadSymbol(lock, "AHardwareBuffer_lock");
    LoadSymbol(unlock, "AHardwareBuffer_unlock");
    LoadSymbol(fromHardwareBuffer, "AHardwareBuffer_fromHardwareBuffer");
    LoadSymbol(toHardwareBuffer, "AHardwareBuffer_toHardwareBuffer");
  }
  if (version >= 30) {
    LoadSymbol(fromBitmap, "AndroidBitmap_getHardwareBuffer");
  }
}

}  // namespace tgfx

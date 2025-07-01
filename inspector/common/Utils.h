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

#pragma once
#include <cstdint>
#include <cstring>
#include <memory>
#include "Protocol.h"

namespace inspector {
template <typename T>
T MemRead(const void* ptr) {
  T val;
  memcpy(&val, ptr, sizeof(T));
  return val;
}

template <typename T>
void MemWrite(void* ptr, T val) {
  memcpy(ptr, &val, sizeof(T));
}

template <typename T>
void MemWrite(void* ptr, T* val, size_t size) {
  memcpy(ptr, val, size);
}

uint64_t GetPid();
const char* GetProcessName();
BroadcastMessage GetBroadcastMessage(const char* procname, size_t pnsz, size_t& len, uint16_t port,
                                     uint8_t type);
}  // namespace inspector
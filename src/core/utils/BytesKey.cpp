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

#include "tgfx/core/BytesKey.h"
#include <cstring>
#include "core/utils/HashRange.h"

namespace tgfx {
union DataConverter {
  float floatValue;
  int intValue;
  uint8_t bytes[4];
  uint32_t uintValue;
};

union PointerConverter {
  const void* pointer;
  uint32_t uintValues[2];
};

void BytesKey::write(uint32_t value) {
  values.push_back(value);
}

void BytesKey::write(int value) {
  DataConverter converter = {};
  converter.intValue = value;
  values.push_back(converter.uintValue);
}

void BytesKey::write(const void* value) {
  PointerConverter converter = {};
  converter.pointer = value;
  values.push_back(converter.uintValues[0]);
  static size_t size = sizeof(intptr_t);
  if (size > 4) {
    values.push_back(converter.uintValues[1]);
  }
}

void BytesKey::write(const uint8_t value[4]) {
  DataConverter converter = {};
  memcpy(converter.bytes, value, 4);
  values.push_back(converter.uintValue);
}

void BytesKey::write(float value) {
  DataConverter converter = {};
  converter.floatValue = value;
  values.push_back(converter.uintValue);
}

size_t BytesKeyHasher::operator()(const BytesKey& key) const {
  return HashRange(key.values.data(), key.values.size());
}
}  // namespace tgfx

/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "HashRange.h"

namespace tgfx {
uint32_t HashRange(const uint32_t* values, size_t count) {
  auto hash = static_cast<uint32_t>(count);
  for (size_t i = 0; i < count; ++i) {
    hash ^= values[i] + 0x9e3779b9 + (hash << 6u) + (hash >> 2u);
  }
  return hash;
}
}  // namespace tgfx

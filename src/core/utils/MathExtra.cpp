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

#include "MathExtra.h"
#include "core/utils/Log.h"

namespace tgfx {

//! Returns the number of leading zero bits (0...32)
// From Hacker's Delight 2nd Edition
constexpr int CLZ_portable(uint32_t x) {
  int n = 32;
  uint32_t y = x >> 16;
  if (y != 0) {
    n -= 16;
    x = y;
  }
  y = x >> 8;
  if (y != 0) {
    n -= 8;
    x = y;
  }
  y = x >> 4;
  if (y != 0) {
    n -= 4;
    x = y;
  }
  y = x >> 2;
  if (y != 0) {
    n -= 2;
    x = y;
  }
  y = x >> 1;
  if (y != 0) {
    return n - 2;
  }
  return n - static_cast<int>(x);
}

//! Returns the number of trailing zero bits (0...32)
// From Hacker's Delight 2nd Edition
constexpr int CTZ_portable(uint32_t x) {
  return 32 - CLZ_portable(~x & (x - 1));
}

#if defined(_MSC_VER)
#include <intrin.h>

static inline int CLZ(uint32_t mask) {
  if (mask) {
    unsigned long index = 0;
    _BitScanReverse(&index, mask);
    // Suppress this bogus /analyze warning. The check for non-zero
    // guarantees that _BitScanReverse will succeed.
#pragma warning(push)
#pragma warning(suppress : 6102)  // Using 'index' from failed function call
    return static_cast<int>(index ^ 0x1F);
#pragma warning(pop)
  } else {
    return 32;
  }
}
#elif defined(__GNUC__) || defined(__clang__)
static inline int CLZ(uint32_t mask) {
  // __builtin_clz(0) is undefined, so we have to detect that case.
  return mask ? __builtin_clz(mask) : 32;
}
#else
static inline int CLZ(uint32_t mask) {
  return CLZ_portable(mask);
}
#endif

int NextLog2(uint32_t value) {
  DEBUG_ASSERT(value != 0);
  return 32 - CLZ(value - 1);
}

int NextPow2(int value) {
  DEBUG_ASSERT(value > 0);
  return 1 << NextLog2(static_cast<uint32_t>(value));
}
}  // namespace tgfx

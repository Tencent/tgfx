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

#pragma once

#include <cmath>
#include <cstring>
#include "core/utils/Log.h"

namespace tgfx {
static constexpr float M_PI_F = static_cast<float>(M_PI);
static constexpr float M_PI_2_F = static_cast<float>(M_PI_2);
static constexpr float FLOAT_NEARLY_ZERO = 1.0f / (1 << 12);
static constexpr float FLOAT_SQRT2 = 1.41421356f;

inline float DegreesToRadians(float degrees) {
  return degrees * (static_cast<float>(M_PI) / 180.0f);
}

inline float RadiansToDegrees(float radians) {
  return radians * (180.0f / static_cast<float>(M_PI));
}

inline bool FloatNearlyZero(float x, float tolerance = FLOAT_NEARLY_ZERO) {
  return fabsf(x) <= tolerance;
}

inline bool FloatNearlyEqual(float x, float y, float tolerance = FLOAT_NEARLY_ZERO) {
  return fabsf(x - y) <= tolerance;
}

inline float SinSnapToZero(float radians) {
  float v = sinf(radians);
  return FloatNearlyZero(v) ? 0.0f : v;
}

inline float CosSnapToZero(float radians) {
  float v = cosf(radians);
  return FloatNearlyZero(v) ? 0.0f : v;
}

inline bool FloatsAreFinite(const float array[], int count) {
  float prod = 0;
  for (int i = 0; i < count; ++i) {
    prod *= array[i];
  }
  return prod == 0;
}

/** Convert a sign-bit int (i.e. float interpreted as int) into a 2s compliement
    int. This also converts -0 (0x80000000) to 0. Doing this to a float allows
    it to be compared using normal C operators (<, <=, etc.)
*/
inline int32_t SignBitTo2sCompliment(int32_t x) {
  if (x < 0) {
    x &= 0x7FFFFFFF;
    x = -x;
  }
  return x;
}

// from http://randomascii.wordpress.com/2012/02/25/comparing-floating-point-numbers-2012-edition/
/*
 * This function is used to compare two floating point numbers for equality
 * within a certain number of ULPs (units in the last place). It is useful
 * for comparing floating point numbers that may have small rounding errors.
 */
inline bool AreWithinUlps(float a, float b, int epsilon) {
  int32_t ia, ib;
  memcpy(&ia, &a, sizeof(float));
  memcpy(&ib, &b, sizeof(float));
  ia = SignBitTo2sCompliment(ia);
  ib = SignBitTo2sCompliment(ib);
  // Find the difference in ULPs.
  return ia < ib + epsilon && ib < ia + epsilon;
}

/**
 * Returns true if value is a power of 2. Does not explicitly check for value <= 0.
 */
template <typename T>
constexpr inline bool IsPow2(T value) {
  return (value & (value - 1)) == 0;
}

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
#elif defined(SK_CPU_ARM32) || defined(__GNUC__) || defined(__clang__)
static inline int CLZ(uint32_t mask) {
  // __builtin_clz(0) is undefined, so we have to detect that case.
  return mask ? __builtin_clz(mask) : 32;
}
#else
static inline int CLZ(uint32_t mask) {
  return CLZ_portable(mask);
}
#endif

/**
 *  Returns the log2 of the specified value, were that value to be rounded up
 *  to the next power of 2. It is undefined to pass 0. Examples:
 *  NextLog2(1) -> 0
 *  NextLog2(2) -> 1
 *  NextLog2(3) -> 2
 *  NextLog2(4) -> 2
 *  NextLog2(5) -> 3
 */
static inline int NextLog2(uint32_t value) {
  DEBUG_ASSERT(value != 0);
  return 32 - CLZ(value - 1);
}

/**
 *  Returns the smallest power-of-2 that is >= the specified value. If value
 *  is already a power of 2, then it is returned unchanged. It is undefined
 *  if value is <= 0.
 */
static inline int NextPow2(int value) {
  DEBUG_ASSERT(value > 0);
  return 1 << NextLog2(static_cast<uint32_t>(value));
}
}  // namespace tgfx

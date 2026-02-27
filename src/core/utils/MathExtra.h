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

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace tgfx {
static constexpr float M_PI_F = static_cast<float>(M_PI);
static constexpr float M_PI_2_F = static_cast<float>(M_PI_2);
static constexpr float FLOAT_NEARLY_ZERO = 1.0f / (1 << 12);
static constexpr float FLOAT_SQRT2 = 1.41421356f;
static constexpr float MaxS32FitsInFloat = 2147483520.f;
static constexpr float MinS32FitsInFloat = -MaxS32FitsInFloat;

// Helper to see a float as its bit pattern (w/o aliasing warnings)
inline uint32_t Float2Bits(float value) {
  uint32_t bits;
  memcpy(&bits, &value, sizeof(uint32_t));
  return bits;
}

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

inline int32_t FloatAs2sCompliment(float x) {
  return SignBitTo2sCompliment((int32_t)Float2Bits(x));
}

#define ScalarAs2sCompliment(x) FloatAs2sCompliment(x)

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
constexpr bool IsPow2(T value) {
  return (value & (value - 1)) == 0;
}

/**
 *  Returns the log2 of the specified value, were that value to be rounded up
 *  to the next power of 2. It is undefined to pass 0. Examples:
 *  NextLog2(1) -> 0
 *  NextLog2(2) -> 1
 *  NextLog2(3) -> 2
 *  NextLog2(4) -> 2
 *  NextLog2(5) -> 3
 */
int NextLog2(uint32_t value);

/**
 * Converts an IEEE 754-2008 half-precision (16-bit) float to single-precision (32-bit) float.
 */
float HalfToFloat(uint16_t value);

/**
 *  Returns the smallest power-of-2 that is >= the specified value. If value
 *  is already a power of 2, then it is returned unchanged. It is undefined
 *  if value is <= 0.
 */
int NextPow2(int value);

inline bool IsInteger(float f) {
  return std::floor(f) == f;
}

#if !defined(TGFX_ATTRIBUTE)
#if defined(__clang__) || defined(__GNUC__)
#define TGFX_ATTRIBUTE(attr) __attribute__((attr))
#else
#define TGFX_ATTRIBUTE(attr)
#endif
#endif

/**
 * IEEE defines how floating-point division behaves for any values and zero denominators, but C does
 * not. Therefore, we provide a helper to suppress possible undefined-behavior warnings.
 */
TGFX_ATTRIBUTE(no_sanitize("float-divide-by-zero"))
inline float IEEEFloatDivide(float numer, float denom) {
  return numer / denom;
}

inline int FloatSaturateToInt(float x) {
  return static_cast<int>(std::clamp(x, MinS32FitsInFloat, MaxS32FitsInFloat));
}

inline int FloatFloorToInt(float x) {
  return FloatSaturateToInt(std::floor(x));
}

inline int FloatCeilToInt(float x) {
  return FloatSaturateToInt(std::ceil(x));
}

inline int FloatRoundToInt(float x) {
  return FloatSaturateToInt(std::round(x));
}

}  // namespace tgfx

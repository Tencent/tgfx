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

#include <cstdint>
#include "tgfx/core/Color.h"
#include "tgfx/gpu/PixelFormat.h"

namespace tgfx {
class Swizzle {
 public:
  static Swizzle ForRead(PixelFormat pixelFormat);

  static Swizzle ForWrite(PixelFormat pixelFormat);

  constexpr Swizzle() : Swizzle("rgba") {
  }

  constexpr bool operator==(const Swizzle& that) const {
    return key == that.key;
  }
  constexpr bool operator!=(const Swizzle& that) const {
    return !(*this == that);
  }

  /**
   * Compact representation of the swizzle suitable for a key.
   */
  constexpr uint16_t asKey() const {
    return key;
  }

  /**
   * 4 char null terminated string consisting only of chars 'r', 'g', 'b', 'a'.
   */
  const char* c_str() const {
    return swiz;
  }

  char operator[](int i) const {
    return swiz[i];
  }

  static constexpr Swizzle RGBA() {
    return Swizzle("rgba");
  }

  static constexpr Swizzle AAAA() {
    return Swizzle("aaaa");
  }

  static constexpr Swizzle RRRR() {
    return Swizzle("rrrr");
  }

  static constexpr Swizzle RRRA() {
    return Swizzle("rrra");
  }

  static constexpr Swizzle RGRG() {
    return Swizzle("rgrg");
  }

  static constexpr Swizzle RARA() {
    return Swizzle("rara");
  }

  Color applyTo(const Color& color) const;

 private:
  char swiz[5];
  uint16_t key = 0;

  static constexpr int CToI(char c) {
    switch (c) {
      case 'r':
        return 0;
      case 'g':
        return 1;
      case 'b':
        return 2;
      case 'a':
        return 3;
      case '1':
        return 4;
      default:
        return -1;
    }
  }

  static constexpr uint16_t MakeKey(const char c[4]) {
    return static_cast<uint16_t>((CToI(c[0]) << 0) | (CToI(c[1]) << 4) | (CToI(c[2]) << 8) |
                                 (CToI(c[3]) << 12));
  }

  constexpr explicit Swizzle(const char c[4])
      : swiz{c[0], c[1], c[2], c[3], '\0'}, key(MakeKey(c)) {
  }
};
}  // namespace tgfx

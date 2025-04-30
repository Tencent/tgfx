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

#include <array>
#include "tgfx/core/WriteStream.h"

namespace tgfx {

class HexadecimalDigits {
 public:
  static inline const std::array<char, 16> upper = {'0', '1', '2', '3', '4', '5', '6', '7',
                                                    '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};

  static inline const std::array<char, 16> lower = {'0', '1', '2', '3', '4', '5', '6', '7',
                                                    '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
};

class MD5 : public WriteStream {
 public:
  MD5();
  bool write(const void* data, size_t size) final;

  size_t bytesWritten() const final {
    return static_cast<size_t>(this->byteCount);
  }

  struct Digest {
    std::string toHexString() const;

    std::string toLowercaseHexString() const;

    bool operator==(Digest const& other) const {
      return 0 == memcmp(data, other.data, sizeof(data));
    }

    bool operator!=(Digest const& other) const {
      return !(*this == other);
    }

    uint8_t data[16];
  };

  Digest finish();

  void flush() final{};

 private:
  uint64_t byteCount;  // number of bytes, modulo 2^64
  uint32_t state[4];   // state (ABCD)
  uint8_t buffer[64];  // input buffer
};

}  // namespace tgfx
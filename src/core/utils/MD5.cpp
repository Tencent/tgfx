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

namespace tgfx {}  // namespace tgfx

#include "tgfx/core/MD5.h"

namespace tgfx {

namespace {
struct F {
  uint32_t operator()(uint32_t x, uint32_t y, uint32_t z) {
    return ((y ^ z) & x) ^ z;
  }
};

struct G {
  uint32_t operator()(uint32_t x, uint32_t y, uint32_t z) {
    return (x & z) | (y & (~z));
  }
};

struct H {
  uint32_t operator()(uint32_t x, uint32_t y, uint32_t z) {
    return x ^ y ^ z;
  }
};

struct I {
  uint32_t operator()(uint32_t x, uint32_t y, uint32_t z) {
    return y ^ (x | (~z));
  }
};

/** Rotates x left n bits. */
inline uint32_t RotateLeft(uint32_t x, uint8_t n) {
  return (x << n) | (x >> (32 - n));
}

template <typename T>
inline void Operation(T operation, uint32_t& a, uint32_t b, uint32_t c, uint32_t d, uint32_t x,
                      uint8_t s, uint32_t t) {
  a = b + RotateLeft(a + operation(b, c, d) + x + t, s);
}

void Encode(uint8_t output[16], const uint32_t input[4]) {
  for (size_t i = 0, j = 0; i < 4; i++, j += 4) {
    output[j] = static_cast<uint8_t>(input[i] & 0xff);
    output[j + 1] = static_cast<uint8_t>((input[i] >> 8) & 0xff);
    output[j + 2] = static_cast<uint8_t>((input[i] >> 16) & 0xff);
    output[j + 3] = static_cast<uint8_t>((input[i] >> 24) & 0xff);
  }
}

void Encode(uint8_t output[8], const uint64_t input) {
  output[0] = static_cast<uint8_t>(input & 0xff);
  output[1] = static_cast<uint8_t>((input >> 8) & 0xff);
  output[2] = static_cast<uint8_t>((input >> 16) & 0xff);
  output[3] = static_cast<uint8_t>((input >> 24) & 0xff);
  output[4] = static_cast<uint8_t>((input >> 32) & 0xff);
  output[5] = static_cast<uint8_t>((input >> 40) & 0xff);
  output[6] = static_cast<uint8_t>((input >> 48) & 0xff);
  output[7] = static_cast<uint8_t>((input >> 56) & 0xff);
}

inline bool IsAligned(const void* pointer, size_t byte_count) {
  return reinterpret_cast<uintptr_t>(pointer) % byte_count == 0;
}

const uint32_t* Decode(uint32_t storage[16], const uint8_t input[64]) {
  if (IsAligned(input, 4)) {
    return reinterpret_cast<const uint32_t*>(input);
  }
  for (size_t i = 0, j = 0; j < 64; i++, j += 4) {
    storage[i] = (static_cast<uint32_t>(input[j])) | ((static_cast<uint32_t>(input[j + 1])) << 8) |
                 ((static_cast<uint32_t>(input[j + 2])) << 16) |
                 ((static_cast<uint32_t>(input[j + 3])) << 24);
  }
  return storage;
}

void Transform(uint32_t state[4], const uint8_t block[64]) {
  uint32_t a = state[0];
  uint32_t b = state[1];
  uint32_t c = state[2];
  uint32_t d = state[3];

  uint32_t storage[16];
  const uint32_t* X = Decode(storage, block);

  // Round 1
  Operation(F(), a, b, c, d, X[0], 7, 0xd76aa478);    // 1
  Operation(F(), d, a, b, c, X[1], 12, 0xe8c7b756);   // 2
  Operation(F(), c, d, a, b, X[2], 17, 0x242070db);   // 3
  Operation(F(), b, c, d, a, X[3], 22, 0xc1bdceee);   // 4
  Operation(F(), a, b, c, d, X[4], 7, 0xf57c0faf);    // 5
  Operation(F(), d, a, b, c, X[5], 12, 0x4787c62a);   // 6
  Operation(F(), c, d, a, b, X[6], 17, 0xa8304613);   // 7
  Operation(F(), b, c, d, a, X[7], 22, 0xfd469501);   // 8
  Operation(F(), a, b, c, d, X[8], 7, 0x698098d8);    // 9
  Operation(F(), d, a, b, c, X[9], 12, 0x8b44f7af);   // 10
  Operation(F(), c, d, a, b, X[10], 17, 0xffff5bb1);  // 11
  Operation(F(), b, c, d, a, X[11], 22, 0x895cd7be);  // 12
  Operation(F(), a, b, c, d, X[12], 7, 0x6b901122);   // 13
  Operation(F(), d, a, b, c, X[13], 12, 0xfd987193);  // 14
  Operation(F(), c, d, a, b, X[14], 17, 0xa679438e);  // 15
  Operation(F(), b, c, d, a, X[15], 22, 0x49b40821);  // 16

  // Round 2
  Operation(G(), a, b, c, d, X[1], 5, 0xf61e2562);    // 17
  Operation(G(), d, a, b, c, X[6], 9, 0xc040b340);    // 18
  Operation(G(), c, d, a, b, X[11], 14, 0x265e5a51);  // 19
  Operation(G(), b, c, d, a, X[0], 20, 0xe9b6c7aa);   // 20
  Operation(G(), a, b, c, d, X[5], 5, 0xd62f105d);    // 21
  Operation(G(), d, a, b, c, X[10], 9, 0x2441453);    // 22
  Operation(G(), c, d, a, b, X[15], 14, 0xd8a1e681);  // 23
  Operation(G(), b, c, d, a, X[4], 20, 0xe7d3fbc8);   // 24
  Operation(G(), a, b, c, d, X[9], 5, 0x21e1cde6);    // 25
  Operation(G(), d, a, b, c, X[14], 9, 0xc33707d6);   // 26
  Operation(G(), c, d, a, b, X[3], 14, 0xf4d50d87);   // 27
  Operation(G(), b, c, d, a, X[8], 20, 0x455a14ed);   // 28
  Operation(G(), a, b, c, d, X[13], 5, 0xa9e3e905);   // 29
  Operation(G(), d, a, b, c, X[2], 9, 0xfcefa3f8);    // 30
  Operation(G(), c, d, a, b, X[7], 14, 0x676f02d9);   // 31
  Operation(G(), b, c, d, a, X[12], 20, 0x8d2a4c8a);  // 32

  // Round 3
  Operation(H(), a, b, c, d, X[5], 4, 0xfffa3942);    // 33
  Operation(H(), d, a, b, c, X[8], 11, 0x8771f681);   // 34
  Operation(H(), c, d, a, b, X[11], 16, 0x6d9d6122);  // 35
  Operation(H(), b, c, d, a, X[14], 23, 0xfde5380c);  // 36
  Operation(H(), a, b, c, d, X[1], 4, 0xa4beea44);    // 37
  Operation(H(), d, a, b, c, X[4], 11, 0x4bdecfa9);   // 38
  Operation(H(), c, d, a, b, X[7], 16, 0xf6bb4b60);   // 39
  Operation(H(), b, c, d, a, X[10], 23, 0xbebfbc70);  // 40
  Operation(H(), a, b, c, d, X[13], 4, 0x289b7ec6);   // 41
  Operation(H(), d, a, b, c, X[0], 11, 0xeaa127fa);   // 42
  Operation(H(), c, d, a, b, X[3], 16, 0xd4ef3085);   // 43
  Operation(H(), b, c, d, a, X[6], 23, 0x4881d05);    // 44
  Operation(H(), a, b, c, d, X[9], 4, 0xd9d4d039);    // 45
  Operation(H(), d, a, b, c, X[12], 11, 0xe6db99e5);  // 46
  Operation(H(), c, d, a, b, X[15], 16, 0x1fa27cf8);  // 47
  Operation(H(), b, c, d, a, X[2], 23, 0xc4ac5665);   // 48

  // Round 4
  Operation(I(), a, b, c, d, X[0], 6, 0xf4292244);    // 49
  Operation(I(), d, a, b, c, X[7], 10, 0x432aff97);   // 50
  Operation(I(), c, d, a, b, X[14], 15, 0xab9423a7);  // 51
  Operation(I(), b, c, d, a, X[5], 21, 0xfc93a039);   // 52
  Operation(I(), a, b, c, d, X[12], 6, 0x655b59c3);   // 53
  Operation(I(), d, a, b, c, X[3], 10, 0x8f0ccc92);   // 54
  Operation(I(), c, d, a, b, X[10], 15, 0xffeff47d);  // 55
  Operation(I(), b, c, d, a, X[1], 21, 0x85845dd1);   // 56
  Operation(I(), a, b, c, d, X[8], 6, 0x6fa87e4f);    // 57
  Operation(I(), d, a, b, c, X[15], 10, 0xfe2ce6e0);  // 58
  Operation(I(), c, d, a, b, X[6], 15, 0xa3014314);   // 59
  Operation(I(), b, c, d, a, X[13], 21, 0x4e0811a1);  // 60
  Operation(I(), a, b, c, d, X[4], 6, 0xf7537e82);    // 61
  Operation(I(), d, a, b, c, X[11], 10, 0xbd3af235);  // 62
  Operation(I(), c, d, a, b, X[2], 15, 0x2ad7d2bb);   // 63
  Operation(I(), b, c, d, a, X[9], 21, 0xeb86d391);   // 64

  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
}

std::string ToHexString(const uint8_t* data, const std::array<char, 16> hexDigits) {
  std::string hexString(2 * sizeof(MD5::Digest::data), '\0');
  for (size_t i = 0; i < sizeof(MD5::Digest::data); ++i) {
    uint8_t byte = data[i];
    hexString[(2 * i) + 0] = hexDigits[byte >> 4];
    hexString[(2 * i) + 1] = hexDigits[byte & 0xF];
  }
  return hexString;
}

}  // namespace

MD5::MD5() : byteCount(0) {
  // These are magic numbers from the specification.
  this->state[0] = 0x67452301;
  this->state[1] = 0xefcdab89;
  this->state[2] = 0x98badcfe;
  this->state[3] = 0x10325476;
}

bool MD5::write(const void* data, size_t size) {
  const auto* input = reinterpret_cast<const uint8_t*>(data);
  auto bufferIndex = static_cast<uint32_t>(byteCount & 0x3F);
  uint32_t bufferAvailable = 64 - bufferIndex;

  uint32_t inputIndex;
  if (size >= bufferAvailable) {
    if (bufferIndex) {
      memcpy(&this->buffer[bufferIndex], input, bufferAvailable);
      Transform(state, buffer);
      inputIndex = bufferAvailable;
    } else {
      inputIndex = 0;
    }

    for (; inputIndex + 63 < size; inputIndex += 64) {
      Transform(state, &input[inputIndex]);
    }
    bufferIndex = 0;
  } else {
    inputIndex = 0;
  }

  memcpy(&this->buffer[bufferIndex], &input[inputIndex], size - inputIndex);
  byteCount += size;
  return true;
}

MD5::Digest MD5::finish() {
  MD5::Digest digest;
  uint8_t bits[8];
  Encode(bits, byteCount << 3);

  auto bufferIndex = static_cast<uint32_t>(byteCount & 0x3F);
  uint32_t paddingLength = (bufferIndex < 56) ? (56 - bufferIndex) : (120 - bufferIndex);
  static const uint8_t PADDING[64] = {
      0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0,    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0,    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  };
  write(PADDING, paddingLength);
  write(bits, 8);

  Encode(digest.data, state);
  return digest;
}

std::string MD5::Digest::toHexString() const {
  return ToHexString(data, HexadecimalDigits::upper);
}

std::string MD5::Digest::toLowercaseHexString() const {
  return ToHexString(data, HexadecimalDigits::lower);
}

}  // namespace tgfx
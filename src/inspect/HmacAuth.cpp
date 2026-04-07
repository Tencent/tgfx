/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "HmacAuth.h"
#include <cstring>

#ifdef _WIN32
#include <bcrypt.h>
#include <windows.h>
#pragma comment(lib, "bcrypt.lib")
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace tgfx::inspect {

// Compile-time shared key for Inspector HMAC authentication.
static constexpr uint8_t SharedKey[] = {
    0x54, 0x47, 0x46, 0x58, 0x5f, 0x49, 0x4e, 0x53, 0x50, 0x45, 0x43, 0x54, 0x4f, 0x52, 0x5f, 0x4b,
    0x45, 0x59, 0x5f, 0x32, 0x30, 0x32, 0x36, 0x5f, 0x76, 0x31, 0x2e, 0x30, 0x2e, 0x30, 0x00, 0x00};
static constexpr size_t SharedKeySize = 32;

// SHA-256 implementation (FIPS 180-4)

static constexpr uint32_t Sha256K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

static uint32_t rotr(uint32_t x, int n) {
  return (x >> n) | (x << (32 - n));
}

static uint32_t ch(uint32_t x, uint32_t y, uint32_t z) {
  return (x & y) ^ (~x & z);
}

static uint32_t maj(uint32_t x, uint32_t y, uint32_t z) {
  return (x & y) ^ (x & z) ^ (y & z);
}

static uint32_t sigma0(uint32_t x) {
  return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);
}

static uint32_t sigma1(uint32_t x) {
  return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);
}

static uint32_t gamma0(uint32_t x) {
  return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3);
}

static uint32_t gamma1(uint32_t x) {
  return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10);
}

static void storeBigEndian32(uint8_t* dst, uint32_t val) {
  dst[0] = static_cast<uint8_t>(val >> 24);
  dst[1] = static_cast<uint8_t>(val >> 16);
  dst[2] = static_cast<uint8_t>(val >> 8);
  dst[3] = static_cast<uint8_t>(val);
}

static uint32_t loadBigEndian32(const uint8_t* src) {
  return (static_cast<uint32_t>(src[0]) << 24) | (static_cast<uint32_t>(src[1]) << 16) |
         (static_cast<uint32_t>(src[2]) << 8) | static_cast<uint32_t>(src[3]);
}

struct Sha256State {
  uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                   0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  uint8_t block[Sha256BlockSize] = {};
  size_t blockLen = 0;
  uint64_t totalLen = 0;
};

static void sha256ProcessBlock(Sha256State& state) {
  uint32_t w[64] = {};
  for (int i = 0; i < 16; i++) {
    w[i] = loadBigEndian32(state.block + i * 4);
  }
  for (int i = 16; i < 64; i++) {
    w[i] = gamma1(w[i - 2]) + w[i - 7] + gamma0(w[i - 15]) + w[i - 16];
  }

  uint32_t a = state.h[0];
  uint32_t b = state.h[1];
  uint32_t c = state.h[2];
  uint32_t d = state.h[3];
  uint32_t e = state.h[4];
  uint32_t f = state.h[5];
  uint32_t g = state.h[6];
  uint32_t hh = state.h[7];

  for (int i = 0; i < 64; i++) {
    uint32_t t1 = hh + sigma1(e) + ch(e, f, g) + Sha256K[i] + w[i];
    uint32_t t2 = sigma0(a) + maj(a, b, c);
    hh = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }

  state.h[0] += a;
  state.h[1] += b;
  state.h[2] += c;
  state.h[3] += d;
  state.h[4] += e;
  state.h[5] += f;
  state.h[6] += g;
  state.h[7] += hh;
}

static void sha256Update(Sha256State& state, const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    state.block[state.blockLen++] = data[i];
    if (state.blockLen == Sha256BlockSize) {
      sha256ProcessBlock(state);
      state.blockLen = 0;
    }
  }
  state.totalLen += len;
}

static void sha256Final(Sha256State& state, uint8_t out[Sha256DigestSize]) {
  uint64_t bitLen = state.totalLen * 8;
  state.block[state.blockLen++] = 0x80;
  if (state.blockLen > 56) {
    while (state.blockLen < Sha256BlockSize) {
      state.block[state.blockLen++] = 0;
    }
    sha256ProcessBlock(state);
    state.blockLen = 0;
  }
  while (state.blockLen < 56) {
    state.block[state.blockLen++] = 0;
  }
  for (int i = 7; i >= 0; i--) {
    state.block[state.blockLen++] = static_cast<uint8_t>(bitLen >> (i * 8));
  }
  sha256ProcessBlock(state);
  for (int i = 0; i < 8; i++) {
    storeBigEndian32(out + i * 4, state.h[i]);
  }
}

void sha256(const uint8_t* data, size_t len, uint8_t out[Sha256DigestSize]) {
  Sha256State state = {};
  sha256Update(state, data, len);
  sha256Final(state, out);
}

void hmacSha256(const uint8_t* key, size_t keyLen, const uint8_t* data, size_t dataLen,
                uint8_t out[HmacSize]) {
  uint8_t keyBlock[Sha256BlockSize] = {};
  if (keyLen > Sha256BlockSize) {
    sha256(key, keyLen, keyBlock);
  } else {
    memcpy(keyBlock, key, keyLen);
  }

  uint8_t iPad[Sha256BlockSize] = {};
  uint8_t oPad[Sha256BlockSize] = {};
  for (size_t i = 0; i < Sha256BlockSize; i++) {
    iPad[i] = keyBlock[i] ^ 0x36;
    oPad[i] = keyBlock[i] ^ 0x5c;
  }

  // inner hash: SHA256(iPad || data)
  Sha256State inner = {};
  sha256Update(inner, iPad, Sha256BlockSize);
  sha256Update(inner, data, dataLen);
  uint8_t innerDigest[Sha256DigestSize] = {};
  sha256Final(inner, innerDigest);

  // outer hash: SHA256(oPad || innerDigest)
  Sha256State outer = {};
  sha256Update(outer, oPad, Sha256BlockSize);
  sha256Update(outer, innerDigest, Sha256DigestSize);
  sha256Final(outer, out);
}

bool generateChallenge(uint8_t out[ChallengeSize]) {
#ifdef _WIN32
  if (BCryptGenRandom(nullptr, out, ChallengeSize, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
    return false;
  }
#else
  int fd = open("/dev/urandom", O_RDONLY);
  if (fd < 0) {
    return false;
  }
  auto bytesRead = read(fd, out, ChallengeSize);
  close(fd);
  if (bytesRead != static_cast<ssize_t>(ChallengeSize)) {
    return false;
  }
#endif
  return true;
}

bool authenticateClient(Socket* sock) {
  uint8_t challenge[ChallengeSize] = {};
  if (!generateChallenge(challenge)) {
    return false;
  }
  if (sock->sendData(challenge, ChallengeSize) == -1) {
    return false;
  }

  uint8_t response[HmacSize] = {};
  if (!sock->readRaw(response, HmacSize, 5000)) {
    return false;
  }

  uint8_t expected[HmacSize] = {};
  hmacSha256(SharedKey, SharedKeySize, challenge, ChallengeSize, expected);

  // Constant-time comparison to prevent timing attacks.
  uint8_t diff = 0;
  for (size_t i = 0; i < HmacSize; i++) {
    diff |= response[i] ^ expected[i];
  }
  return diff == 0;
}

}  // namespace tgfx::inspect

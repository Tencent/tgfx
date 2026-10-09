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

#include "StageCache.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <system_error>
#if defined(_WIN32)
#include <process.h>
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <unistd.h>
#else
#include <unistd.h>
#endif

namespace tgfx {

namespace {

constexpr std::array<uint32_t, 64> kRoundConstants = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

uint32_t RotateRight(uint32_t value, int bits) {
  return (value >> bits) | (value << (32 - bits));
}

// Bumped whenever the entry layout or the meaning of a key changes.
constexpr uint32_t kSchemaVersion = 1;
constexpr char kEntryMagic[4] = {'T', 'G', 'S', 'C'};
constexpr size_t kEntryHeaderSize = 4 + 4 + 8 + 32;

void PutLE32(std::vector<uint8_t>* out, uint32_t value) {
  for (int i = 0; i < 4; ++i) {
    out->push_back(static_cast<uint8_t>(value >> (8 * i)));
  }
}

void PutLE64(std::vector<uint8_t>* out, uint64_t value) {
  for (int i = 0; i < 8; ++i) {
    out->push_back(static_cast<uint8_t>(value >> (8 * i)));
  }
}

uint64_t GetLE(const std::vector<uint8_t>& bytes, size_t offset, size_t width) {
  uint64_t value = 0;
  for (size_t i = 0; i < width; ++i) {
    value |= static_cast<uint64_t>(bytes[offset + i]) << (8 * i);
  }
  return value;
}

std::array<uint8_t, 32> Sha256Of(const void* data, size_t size) {
  Sha256 hash;
  hash.update(data, size);
  return hash.finish();
}

std::string ExecutablePath() {
#if defined(_WIN32)
  std::vector<char> buffer(32768);
  auto length = GetModuleFileNameA(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  if (length == 0 || length >= buffer.size()) {
    return "";
  }
  return std::string(buffer.data(), length);
#elif defined(__APPLE__)
  uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  std::vector<char> buffer(size + 1, '\0');
  if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
    return "";
  }
  return std::string(buffer.data());
#else
  std::vector<char> buffer(4096, '\0');
  auto length = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
  if (length <= 0) {
    return "";
  }
  return std::string(buffer.data(), static_cast<size_t>(length));
#endif
}

uint64_t ProcessId() {
#if defined(_WIN32)
  return static_cast<uint64_t>(_getpid());
#else
  return static_cast<uint64_t>(getpid());
#endif
}

}  // namespace

Sha256::Sha256()
    : state({0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab,
             0x5be0cd19}) {
}

void Sha256::transform(const uint8_t* block) {
  std::array<uint32_t, 64> w = {};
  for (size_t i = 0; i < 16; ++i) {
    w[i] = (static_cast<uint32_t>(block[4 * i]) << 24) |
           (static_cast<uint32_t>(block[4 * i + 1]) << 16) |
           (static_cast<uint32_t>(block[4 * i + 2]) << 8) | static_cast<uint32_t>(block[4 * i + 3]);
  }
  for (size_t i = 16; i < 64; ++i) {
    uint32_t s0 = RotateRight(w[i - 15], 7) ^ RotateRight(w[i - 15], 18) ^ (w[i - 15] >> 3);
    uint32_t s1 = RotateRight(w[i - 2], 17) ^ RotateRight(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
  uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
  for (size_t i = 0; i < 64; ++i) {
    uint32_t s1 = RotateRight(e, 6) ^ RotateRight(e, 11) ^ RotateRight(e, 25);
    uint32_t choose = (e & f) ^ (~e & g);
    uint32_t t1 = h + s1 + choose + kRoundConstants[i] + w[i];
    uint32_t s0 = RotateRight(a, 2) ^ RotateRight(a, 13) ^ RotateRight(a, 22);
    uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
    uint32_t t2 = s0 + majority;
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }
  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
  state[4] += e;
  state[5] += f;
  state[6] += g;
  state[7] += h;
}

void Sha256::update(const void* data, size_t size) {
  auto bytes = static_cast<const uint8_t*>(data);
  totalBytes += size;
  while (size > 0) {
    size_t take = std::min(size, buffer.size() - bufferSize);
    std::memcpy(buffer.data() + bufferSize, bytes, take);
    bufferSize += take;
    bytes += take;
    size -= take;
    if (bufferSize == buffer.size()) {
      transform(buffer.data());
      bufferSize = 0;
    }
  }
}

std::array<uint8_t, 32> Sha256::finish() {
  uint64_t bitLength = totalBytes * 8;
  uint8_t pad = 0x80;
  update(&pad, 1);
  uint8_t zero = 0;
  while (bufferSize != 56) {
    update(&zero, 1);
  }
  uint8_t lengthBytes[8];
  for (int i = 0; i < 8; ++i) {
    lengthBytes[i] = static_cast<uint8_t>(bitLength >> (56 - 8 * i));
  }
  update(lengthBytes, 8);
  std::array<uint8_t, 32> digest = {};
  for (size_t i = 0; i < 8; ++i) {
    for (size_t j = 0; j < 4; ++j) {
      digest[4 * i + j] = static_cast<uint8_t>(state[i] >> (24 - 8 * j));
    }
  }
  return digest;
}

std::string Sha256::Hex(const std::array<uint8_t, 32>& digest) {
  static const char* digits = "0123456789abcdef";
  std::string text;
  for (auto byte : digest) {
    text += digits[byte >> 4];
    text += digits[byte & 0xF];
  }
  return text;
}

bool Sha256SelfTest() {
  struct Vector {
    std::string input;
    const char* expected;
  };
  const Vector vectors[] = {
      {"", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
      {"abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
      {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
       "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"},
      {std::string(1000000, 'a'), "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"},
  };
  for (const auto& vector : vectors) {
    if (Sha256::Hex(Sha256Of(vector.input.data(), vector.input.size())) != vector.expected) {
      return false;
    }
  }
  return true;
}

StageCacheKey::StageCacheKey(const std::string& kind) {
  add(static_cast<uint64_t>(kSchemaVersion));
  add(StageCache::Get().toolIdentity());
  add(kind);
}

StageCacheKey& StageCacheKey::add(const void* data, size_t size) {
  uint64_t length = size;
  uint8_t prefix[8];
  for (int i = 0; i < 8; ++i) {
    prefix[i] = static_cast<uint8_t>(length >> (8 * i));
  }
  hash.update(prefix, 8);
  hash.update(data, size);
  return *this;
}

StageCacheKey& StageCacheKey::add(const std::string& text) {
  return add(text.data(), text.size());
}

StageCacheKey& StageCacheKey::add(uint64_t value) {
  uint8_t bytes[8];
  for (int i = 0; i < 8; ++i) {
    bytes[i] = static_cast<uint8_t>(value >> (8 * i));
  }
  return add(bytes, 8);
}

std::string StageCacheKey::finish() {
  return Sha256::Hex(hash.finish());
}

StageCache::StageCache() = default;

StageCache& StageCache::Get() {
  static StageCache cache;
  return cache;
}

bool StageCache::open(const std::string& dir, std::string* error) {
  directory.clear();
  identity.clear();
  if (!Sha256SelfTest()) {
    *error = "SHA-256 self-test failed";
    return false;
  }
  auto executable = ExecutablePath();
  if (executable.empty()) {
    *error = "cannot locate the shader_build_tool executable to identify it";
    return false;
  }
  std::ifstream file(executable, std::ios::binary);
  if (!file.is_open()) {
    *error = "cannot read " + executable + " to identify it";
    return false;
  }
  Sha256 toolHash;
  std::vector<char> chunk(1 << 20);
  while (file) {
    file.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
    toolHash.update(chunk.data(), static_cast<size_t>(file.gcount()));
  }
  std::error_code code;
  std::filesystem::create_directories(dir, code);
  if (code) {
    *error = "cannot create " + dir + ": " + code.message();
    return false;
  }
  identity = Sha256::Hex(toolHash.finish());
  directory = dir;
  return true;
}

std::string StageCache::entryPath(const std::string& key) const {
  return directory + "/" + key.substr(0, 2) + "/" + key;
}

bool StageCache::load(const std::string& key, std::vector<uint8_t>* payload,
                      StageCacheStats* stats) {
  if (!persistent()) {
    return false;
  }
  std::ifstream file(entryPath(key), std::ios::binary);
  if (!file.is_open()) {
    return false;
  }
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
  bool valid = bytes.size() >= kEntryHeaderSize &&
               std::memcmp(bytes.data(), kEntryMagic, 4) == 0 &&
               GetLE(bytes, 4, 4) == kSchemaVersion &&
               GetLE(bytes, 8, 8) == bytes.size() - kEntryHeaderSize;
  if (valid) {
    auto digest = Sha256Of(bytes.data() + kEntryHeaderSize, bytes.size() - kEntryHeaderSize);
    valid = std::memcmp(digest.data(), bytes.data() + 16, 32) == 0;
  }
  if (!valid) {
    stats->corrupt++;
    return false;
  }
  payload->assign(bytes.begin() + static_cast<std::ptrdiff_t>(kEntryHeaderSize), bytes.end());
  stats->diskHits++;
  return true;
}

void StageCache::store(const std::string& key, const std::vector<uint8_t>& payload,
                       StageCacheStats* stats) {
  if (!persistent()) {
    return;
  }
  auto path = entryPath(key);
  std::error_code code;
  std::filesystem::create_directories(directory + "/" + key.substr(0, 2), code);
  if (code) {
    return;
  }
  std::vector<uint8_t> bytes(kEntryMagic, kEntryMagic + 4);
  PutLE32(&bytes, kSchemaVersion);
  PutLE64(&bytes, payload.size());
  auto digest = Sha256Of(payload.data(), payload.size());
  bytes.insert(bytes.end(), digest.begin(), digest.end());
  bytes.insert(bytes.end(), payload.begin(), payload.end());
  auto temp = path + ".tmp" + std::to_string(ProcessId()) + "_" + std::to_string(tempCounter++);
  {
    std::ofstream file(temp, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    file.close();
    if (!file.good()) {
      std::filesystem::remove(temp, code);
      return;
    }
  }
  std::filesystem::rename(temp, path, code);
  if (code) {
    std::filesystem::remove(temp, code);
    return;
  }
  stats->stored++;
}

void StageCache::printSummary() const {
  std::cout << "[stage-cache] "
            << (persistent() ? "persistent at " + directory : std::string("memory only"));
  if (persistent()) {
    std::cout << " (tool " << identity.substr(0, 12) << ")";
  }
  std::cout << "\n";
  for (const auto& entry : kindStats) {
    const auto& s = entry.second;
    std::cout << "[stage-cache] " << entry.first << ": requests=" << s.requests
              << " memory-hits=" << s.memoryHits << " disk-hits=" << s.diskHits
              << " executed=" << s.executed << " stored=" << s.stored << " corrupt=" << s.corrupt
              << "\n";
  }
}

}  // namespace tgfx

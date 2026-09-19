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
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. See the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstddef>
#include <cstdint>
#include "tgfx/gpu/Backend.h"

namespace tgfx {

// Shared identity contract between the offline bundle writer (tools/shader_build_tool) and the
// runtime loader (PrecompiledShaderCache). A bundle's identity hash is a 64-bit FNV-1a over
// the serialized pool entries, the uncompressed data pool, the reflection pool, the profile
// tag, and the counts. Any shader-content or reflection-layout change produces a different
// hash even when shader names and permutation indices stay the same, which is exactly the
// compatibility gap a format version cannot express. Both sides must feed the exact same
// field sequence; the helpers below are the single source of truth for that sequence.

/**
 * The toolchain ABI version every bundle records (header offset 16). This is the compatibility
 * gate for everything the content identity hash cannot express on old bundles (hash 0): the
 * reflection layout grammar, the uniform block contracts, and the rule semantics the runtime
 * matchers assume when they turn a pool entry into a program. Bump this constant whenever any of
 * those contracts change in a way that keeps shader names and pool indices stable but changes
 * what the bytes mean; the writer picks the new value up automatically and the loader then
 * refuses every older bundle instead of feeding mismatched layouts to the GPU.
 */
inline constexpr uint32_t kExpectedToolchainABI = 0x00010003u;

inline uint64_t BundleIdentityHashInit() {
  return 0x54475346424E4443ULL;  // "TGSF BNDC"
}

inline uint64_t BundleIdentityHashBytes(uint64_t state, const void* data, size_t size) {
  auto* bytes = reinterpret_cast<const uint8_t*>(data);
  for (size_t i = 0; i < size; ++i) {
    state ^= bytes[i];
    state *= 0x100000001B3ULL;
  }
  return state;
}

// The header-level fields are fed little-endian, matching the on-disk serialization order:
// formatVersion(2) + vertPoolCount(4) + fragPoolCount(4) + profileTag(32, zero padded).
inline uint64_t BundleIdentityHashHeader(uint64_t state, uint16_t formatVersion,
                                         uint32_t vertPoolCount, uint32_t fragPoolCount,
                                         const uint8_t* profileTag32) {
  uint8_t le[4] = {};
  le[0] = static_cast<uint8_t>(formatVersion);
  le[1] = static_cast<uint8_t>(formatVersion >> 8);
  state = BundleIdentityHashBytes(state, le, 2);
  for (uint32_t value : {vertPoolCount, fragPoolCount}) {
    le[0] = static_cast<uint8_t>(value);
    le[1] = static_cast<uint8_t>(value >> 8);
    le[2] = static_cast<uint8_t>(value >> 16);
    le[3] = static_cast<uint8_t>(value >> 24);
    state = BundleIdentityHashBytes(state, le, 4);
  }
  return BundleIdentityHashBytes(state, profileTag32, 32);
}

// Pool entries are fed in the on-disk field order: hashHi(8) + hashLo(8) + dataOffset(4) +
// dataSize(4) + reflOffset(4), all little-endian.
inline uint64_t BundleIdentityHashEntry(uint64_t state, uint64_t hashHi, uint64_t hashLo,
                                        uint32_t dataOffset, uint32_t dataSize,
                                        uint32_t reflOffset) {
  auto feedU64 = [&state](uint64_t value) {
    uint8_t le[8] = {};
    for (int i = 0; i < 8; ++i) {
      le[i] = static_cast<uint8_t>(value >> (i * 8));
    }
    state = BundleIdentityHashBytes(state, le, 8);
  };
  auto feedU32 = [&state](uint32_t value) {
    uint8_t le[4] = {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8),
                     static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 24)};
    state = BundleIdentityHashBytes(state, le, 4);
  };
  feedU64(hashHi);
  feedU64(hashLo);
  feedU32(dataOffset);
  feedU32(dataSize);
  feedU32(reflOffset);
  return state;
}

// Maps a runtime Backend to the profile tag the bundle writer embeds (the --backends CLI
// value). Bundles whose tag does not match the running backend must be rejected: their code
// blobs are compiled for a different shader dialect or binary format, and the format version
// says nothing about that mismatch.
inline const char* ExpectedProfileTag(Backend backend) {
  switch (backend) {
    case Backend::OpenGL:
      return "opengl";
    case Backend::Metal:
      return "metal";
    case Backend::Vulkan:
      return "vulkan";
    case Backend::WebGPU:
      return "webgpu";
    default:
      return "";
  }
}

}  // namespace tgfx

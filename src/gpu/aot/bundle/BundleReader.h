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
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. See the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include "gpu/aot/bundle/BundleData.h"
#include "gpu/shaders/contract/ShaderBundleContract.h"

namespace tgfx {

// The bundle reader: the byte grammar of one bundle (its header and its pools) and nothing else.
// It is a separate translation unit from the cache because it holds no state: the cache decides
// what to parse and what to share, this decides what the bytes mean. The format fact itself lives
// in the contract header, so every reader — this parse, the loader, the tool's writer — addresses
// the same offsets.

/// Checks a pool's entries against the data pool's bounds and the payload budget, without loading.
bool CheckPoolPayloadBudget(const uint8_t* fileData, size_t poolOffset, uint32_t poolCount,
                            size_t dataPoolSize, size_t* remainingBytes);

/// Loads one pool's stages into `entries`.
bool LoadPool(const uint8_t* fileData, size_t fileSize, size_t poolOffset, uint32_t poolCount,
              size_t dataPoolStart, size_t dataPoolEnd,
              std::unordered_map<uint32_t, ShaderStageBlob>& entries);

/// Little-endian byte readers over the bundle's on-disk layout. The format fact lives in one place
/// (ShaderBundleContract.h): the loader, this header's parser and every other reader address the
/// same offsets, so two readers cannot disagree about the layout.

inline uint16_t ReadBundleU16LE(const uint8_t* data, size_t offset) {
  return static_cast<uint16_t>(data[offset] | (data[offset + 1] << 8));
}

inline uint32_t ReadBundleU32LE(const uint8_t* data, size_t offset) {
  return static_cast<uint32_t>(data[offset]) | (static_cast<uint32_t>(data[offset + 1]) << 8) |
         (static_cast<uint32_t>(data[offset + 2]) << 16) |
         (static_cast<uint32_t>(data[offset + 3]) << 24);
}

inline uint64_t ReadBundleU64LE(const uint8_t* data, size_t offset) {
  return static_cast<uint64_t>(ReadBundleU32LE(data, offset)) |
         (static_cast<uint64_t>(ReadBundleU32LE(data, offset + 4)) << 32);
}

/**
 * The bundle file header as every reader sees it. The field layout is BundleFormat's (contract
 * header); this struct is the one extraction of it. Byte order is little-endian, as the format
 * specifies. The bundle carries no interface description: the runtime derives every layout number
 * from the declaration table it was compiled from.
 */
struct BundleHeader {
  uint32_t magic = 0;
  uint16_t formatVersion = 0;
  uint16_t compressionType = 0;
  uint64_t identityHash = 0;
  uint32_t toolchainABI = 0;
  uint32_t vertPoolCount = 0;
  uint32_t fragPoolCount = 0;
  uint32_t vertPoolOffset = 0;
  uint32_t fragPoolOffset = 0;
  uint32_t dataOffset = 0;
  uint32_t dataSize = 0;
  std::string profileTag;
  /// What the data pool holds, one of BundleFormat::Payload.
  uint16_t payloadFormat = 0;
};

/**
 * Extracts the header fields. Callers check the size before parsing (the format's own minimum is
 * BundleFormat::HeaderSize); this returns false for a null buffer or a short one so the check can
 * also live here.
 */
bool ParseBundleHeader(const uint8_t* data, size_t size, BundleHeader* header);

/// Feeds a pool's entry hashes into the bundle identity hash, in the on-disk field order the
/// writer feeds (see BundleIdentityHashEntry).
uint64_t HashPoolEntries(uint64_t state, const uint8_t* pool, uint32_t count);

}  // namespace tgfx

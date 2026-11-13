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
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include "gpu/shaders/contract/ShaderBundleContract.h"

namespace tgfx {

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

}  // namespace tgfx

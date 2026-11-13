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
#include <unordered_map>
#include "gpu/aot/bundle/BundleData.h"

namespace tgfx {

// The bundle reader: byte-level parsing of one bundle's pools. It is a separate translation unit
// from the cache because it holds no state: the cache decides what to parse and what to share,
// this decides what the bytes mean.

/// Checks a pool's entries against the data pool's bounds and the payload budget, without loading.
bool CheckPoolPayloadBudget(const uint8_t* fileData, size_t poolOffset, uint32_t poolCount,
                            size_t dataPoolSize, size_t* remainingBytes);

/// Loads one pool's stages into `entries`.
bool LoadPool(const uint8_t* fileData, size_t fileSize, size_t poolOffset, uint32_t poolCount,
              size_t dataPoolStart, size_t dataPoolEnd,
              std::unordered_map<uint32_t, ShaderStageBlob>& entries);

/// Feeds a pool's entry hashes into the bundle identity hash, in the on-disk field order the
/// writer feeds (see BundleIdentityHashEntry).
uint64_t HashPoolEntries(uint64_t state, const uint8_t* pool, uint32_t count);

}  // namespace tgfx

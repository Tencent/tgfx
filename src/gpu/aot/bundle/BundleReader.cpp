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

#include "gpu/aot/bundle/BundleReader.h"
#include "core/utils/Log.h"
#include "gpu/aot/bundle/BundleCodec.h"
#include "gpu/shaders/contract/ShaderBundleContract.h"

namespace tgfx {

static bool ConsumePayloadBudget(size_t bytes, size_t* remainingBytes) {
  if (bytes > *remainingBytes) {
    return false;
  }
  *remainingBytes -= bytes;
  return true;
}

// Pool table spans have already been checked. Account for aliases separately: every entry
// becomes an owned blob, even when several keys refer to the same serialized byte range.
bool CheckPoolPayloadBudget(const uint8_t* fileData, size_t poolOffset, uint32_t poolCount,
                            size_t dataPoolSize, size_t* remainingBytes) {
  for (uint32_t index = 0; index < poolCount; ++index) {
    auto entry = fileData + poolOffset + static_cast<size_t>(index) * BundleFormat::PoolEntrySize;
    auto blobOffset = ReadBundleU32LE(entry + BundleFormat::EntryOffset::DataOffset, 0);
    auto blobSize = ReadBundleU32LE(entry + BundleFormat::EntryOffset::DataSize, 0);
    if (blobOffset > dataPoolSize || blobSize > dataPoolSize - blobOffset ||
        !ConsumePayloadBudget(sizeof(ShaderStageBlob) + sizeof(uint32_t), remainingBytes) ||
        !ConsumePayloadBudget(blobSize, remainingBytes)) {
      return false;
    }
  }
  return true;
}

bool LoadPool(const uint8_t* fileData, size_t fileSize, size_t poolOffset, uint32_t poolCount,
              size_t dataPoolStart, size_t dataPoolEnd,
              std::unordered_map<uint32_t, ShaderStageBlob>& entries) {
  for (uint32_t i = 0; i < poolCount; i++) {
    size_t entryOff = poolOffset + static_cast<size_t>(i) * BundleFormat::PoolEntrySize;
    if (entryOff > fileSize || BundleFormat::PoolEntrySize > fileSize - entryOff) {
      LOGE("PrecompiledShaderCache: Pool entry %u out of bounds", i);
      return false;
    }
    const uint8_t* entry = fileData + entryOff;
    uint32_t variantId = ReadBundleU32LE(entry + BundleFormat::EntryOffset::VariantId, 0);
    uint32_t blobOff = ReadBundleU32LE(entry + BundleFormat::EntryOffset::DataOffset, 0);
    uint32_t blobSize = ReadBundleU32LE(entry + BundleFormat::EntryOffset::DataSize, 0);

    auto dataPoolSize = dataPoolEnd - dataPoolStart;
    if (blobOff > dataPoolSize || blobSize > dataPoolSize - blobOff) {
      LOGE("PrecompiledShaderCache: Data blob outside the data pool for entry %u", i);
      return false;
    }
    size_t absDataOff = dataPoolStart + blobOff;

    ShaderStageBlob blob;
    blob.data.assign(fileData + absDataOff, fileData + absDataOff + blobSize);

    uint32_t key = variantId;
    auto existing = entries.find(key);
    if (existing != entries.end()) {
      // A repeated stage key is only legal as byte-identical duplication; the writer rejects
      // conflicting content at build time, so a real conflict here means the bundle was hand-
      // edited or corrupted — reject it instead of silently keeping the last one.
      if (existing->second.data != blob.data) {
        LOGE("PrecompiledShaderCache: Conflicting content for duplicate pool entry (variant %u)",
             variantId);
        return false;
      }
    } else {
      entries[key] = std::move(blob);
    }
  }
  return true;
}

// Feeds the pool entries into the bundle identity hash in the on-disk field order, the sequence the
// writer feeds (see BundleIdentityHashEntry).
uint64_t HashPoolEntries(uint64_t state, const uint8_t* pool, uint32_t count) {
  for (uint32_t i = 0; i < count; i++) {
    const uint8_t* entry = pool + static_cast<size_t>(i) * BundleFormat::PoolEntrySize;
    state = BundleIdentityHashEntry(
        state, ReadBundleU32LE(entry + BundleFormat::EntryOffset::VariantId, 0),
        ReadBundleU32LE(entry + BundleFormat::EntryOffset::DataOffset, 0),
        ReadBundleU32LE(entry + BundleFormat::EntryOffset::DataSize, 0));
  }
  return state;
}

}  // namespace tgfx

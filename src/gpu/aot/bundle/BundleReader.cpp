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

#include "gpu/aot/bundle/BundleReader.h"
#include <cstring>
#include "core/utils/Log.h"
#include "gpu/shaders/BundleCodec.h"
#include "gpu/shaders/contract/ShaderBundleContract.h"

namespace tgfx {

static bool ConsumePayloadBudget(size_t bytes, size_t* remainingBytes) {
  if (bytes > *remainingBytes) {
    return false;
  }
  *remainingBytes -= bytes;
  return true;
}

static bool IsValidReflectionFormat(UniformFormat format, bool sampler) {
  switch (format) {
    case UniformFormat::Float:
    case UniformFormat::Float2:
    case UniformFormat::Float3:
    case UniformFormat::Float4:
    case UniformFormat::Float2x2:
    case UniformFormat::Float3x3:
    case UniformFormat::Float4x4:
    case UniformFormat::Int:
    case UniformFormat::Int2:
    case UniformFormat::Int3:
    case UniformFormat::Int4:
      return !sampler;
    case UniformFormat::Texture2DSampler:
    case UniformFormat::TextureExternalSampler:
    case UniformFormat::Texture2DRectSampler:
      return sampler;
    default:
      return false;
  }
}

bool ParseStageReflection(const uint8_t* data, size_t maxLen, ShaderStageBlob* blob,
                          size_t* remainingBytes) {
  // The byte grammar is single-sourced in BundleCodec (the tool's writer goes through it too);
  // this side keeps the semantic rules the enum owns: which formats may be uniforms or samplers,
  // sampler array sizes, and vertex-input locations that strictly increase.
  BundleReflection reflection = {};
  if (!ParseBundleReflection(data, maxLen, &reflection)) {
    return false;
  }
  size_t budgetBytes = reflection.uniforms.size() * sizeof(Uniform) +
                       reflection.samplers.size() * sizeof(Uniform) +
                       reflection.vertexInputs.size() * sizeof(VertexInput);
  for (const auto* list : {&reflection.uniforms, &reflection.samplers}) {
    for (const auto& entry : *list) {
      budgetBytes += entry.name.size() + 1;
    }
  }
  for (const auto& entry : reflection.vertexInputs) {
    budgetBytes += entry.name.size() + 2;
  }
  if (!ConsumePayloadBudget(budgetBytes, remainingBytes)) {
    return false;
  }
  blob->uniforms.reserve(reflection.uniforms.size());
  for (auto& entry : reflection.uniforms) {
    auto format = static_cast<UniformFormat>(entry.format);
    if (!IsValidReflectionFormat(format, false) || entry.arraySize == 0) {
      return false;
    }
    blob->uniforms.emplace_back(std::move(entry.name), format,
                                static_cast<uint32_t>(entry.arraySize));
  }
  blob->samplers.reserve(reflection.samplers.size());
  for (auto& entry : reflection.samplers) {
    auto format = static_cast<UniformFormat>(entry.format);
    if (!IsValidReflectionFormat(format, true) || entry.arraySize != 1) {
      return false;
    }
    blob->samplers.emplace_back(std::move(entry.name), format, 1u);
  }
  bool hasPreviousLocation = false;
  uint32_t previousLocation = 0;
  blob->vertexInputs.reserve(reflection.vertexInputs.size());
  for (auto& entry : reflection.vertexInputs) {
    auto format = static_cast<UniformFormat>(entry.format);
    if (!IsValidReflectionFormat(format, false)) {
      LOGE("PrecompiledShaderCache: vertex input '%s' has invalid format %u", entry.name.c_str(),
           static_cast<uint32_t>(entry.format));
      return false;
    }
    auto location = static_cast<uint32_t>(entry.location);
    // The entries are stored in location order, so a decreasing or duplicated location means the
    // .vert's own layout declarations (or the serialized order) are inconsistent — the backends
    // bind attribute slots by location, and a duplicate would silently leave one slot unbound.
    // A gap between locations is legal: the GP's optional slots leave holes on purpose
    // (shape-instanced kernels skip location 1), so contiguity is not required.
    if (hasPreviousLocation && location <= previousLocation) {
      LOGE(
          "PrecompiledShaderCache: vertex input '%s' location %u does not increase (previous "
          "%u)",
          entry.name.c_str(), location, previousLocation);
      return false;
    }
    previousLocation = location;
    hasPreviousLocation = true;
    blob->vertexInputs.push_back(VertexInput{std::move(entry.name), format, location});
  }
  blob->specFields.reserve(reflection.specEntries.size());
  for (auto& entry : reflection.specEntries) {
    auto format = static_cast<UniformFormat>(entry.format);
    // Only the two integer shapes can fold: a specialized read must return exactly what the
    // uniform read returned, so float fields never enter the table.
    if (format != UniformFormat::Int && format != UniformFormat::Int4) {
      LOGE("PrecompiledShaderCache: spec field '%s' has a non-integer format %u",
           entry.name.c_str(), static_cast<uint32_t>(entry.format));
      return false;
    }
    if (entry.arraySize == 0) {
      LOGE("PrecompiledShaderCache: spec field '%s' has array size 0", entry.name.c_str());
      return false;
    }
    auto firstId = static_cast<uint32_t>(entry.firstId);
    if (firstId == 0) {
      // Id 0 is the master switch and never belongs to an entry.
      LOGE("PrecompiledShaderCache: spec field '%s' claims the master-switch id 0",
           entry.name.c_str());
      return false;
    }
    if (!ConsumePayloadBudget(entry.name.size() + 6, remainingBytes)) {
      return false;
    }
    blob->specFields.push_back(SpecFieldInfo{std::move(entry.name), format,
                                             static_cast<uint32_t>(entry.arraySize), firstId});
  }
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
              size_t dataPoolStart, size_t dataPoolEnd, size_t reflectionPoolStart,
              std::unordered_map<uint32_t, ShaderStageBlob>& entries, size_t* remainingBytes) {
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
    uint32_t reflOff = ReadBundleU32LE(entry + BundleFormat::EntryOffset::ReflectionOffset, 0);

    auto dataPoolSize = dataPoolEnd - dataPoolStart;
    if (blobOff > dataPoolSize || blobSize > dataPoolSize - blobOff) {
      LOGE("PrecompiledShaderCache: Data blob outside the data pool for entry %u", i);
      return false;
    }
    size_t absDataOff = dataPoolStart + blobOff;

    ShaderStageBlob blob;
    blob.data.assign(fileData + absDataOff, fileData + absDataOff + blobSize);

    if (reflectionPoolStart != 0) {
      if (reflectionPoolStart > fileSize || reflOff >= fileSize - reflectionPoolStart) {
        LOGE("PrecompiledShaderCache: Reflection out of bounds for entry %u", i);
        return false;
      }
      size_t absReflOff = reflectionPoolStart + reflOff;
      if (!ParseStageReflection(fileData + absReflOff, fileSize - absReflOff, &blob,
                                remainingBytes)) {
        LOGE("PrecompiledShaderCache: Failed to parse reflection for entry %u", i);
        return false;
      }
    }

    uint32_t key = variantId;
    auto existing = entries.find(key);
    if (existing != entries.end()) {
      // A repeated stage key is only legal as byte-identical duplication; the writer rejects
      // conflicting content at build time, so a real conflict here means the bundle was hand-
      // edited or corrupted — reject it instead of silently keeping the last one.
      auto uniformListsEqual = [](const std::vector<Uniform>& a, const std::vector<Uniform>& b) {
        if (a.size() != b.size()) {
          return false;
        }
        for (size_t u = 0; u < a.size(); ++u) {
          if (a[u].name() != b[u].name() || a[u].format() != b[u].format() ||
              a[u].arraySize() != b[u].arraySize()) {
            return false;
          }
        }
        return true;
      };
      auto vertexInputsEqual = [](const std::vector<VertexInput>& a,
                                  const std::vector<VertexInput>& b) {
        if (a.size() != b.size()) {
          return false;
        }
        for (size_t u = 0; u < a.size(); ++u) {
          if (a[u].name != b[u].name || a[u].format != b[u].format ||
              a[u].location != b[u].location) {
            return false;
          }
        }
        return true;
      };
      if (existing->second.data != blob.data ||
          !uniformListsEqual(existing->second.uniforms, blob.uniforms) ||
          !uniformListsEqual(existing->second.samplers, blob.samplers) ||
          !vertexInputsEqual(existing->second.vertexInputs, blob.vertexInputs)) {
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
        ReadBundleU32LE(entry + BundleFormat::EntryOffset::DataSize, 0),
        ReadBundleU32LE(entry + BundleFormat::EntryOffset::ReflectionOffset, 0));
  }
  return state;
}

}  // namespace tgfx

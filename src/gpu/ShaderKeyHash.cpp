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

#include "ShaderKeyHash.h"

namespace tgfx {

static constexpr uint64_t FNV1A_SEED_HI = 0x6C62272E07BB0142ULL;
static constexpr uint64_t FNV1A_SEED_LO = 0x62B821756295C58DULL;
static constexpr uint64_t FNV1A_PRIME = 0x00000100000001B3ULL;

static uint64_t Fnv1a64(const std::vector<uint8_t>& data, uint64_t seed) {
  uint64_t hash = seed;
  for (auto byte : data) {
    hash ^= static_cast<uint64_t>(byte);
    hash *= FNV1A_PRIME;
  }
  return hash;
}

static std::vector<uint8_t> SerializeFieldsLE(const std::string& shaderName,
                                              uint32_t permutationIndex,
                                              const std::string& profileTag) {
  std::vector<uint8_t> bytes;
  auto nameLen = static_cast<uint32_t>(shaderName.size());
  bytes.push_back(static_cast<uint8_t>(nameLen & 0xFF));
  bytes.push_back(static_cast<uint8_t>((nameLen >> 8) & 0xFF));
  bytes.push_back(static_cast<uint8_t>((nameLen >> 16) & 0xFF));
  bytes.push_back(static_cast<uint8_t>((nameLen >> 24) & 0xFF));
  bytes.insert(bytes.end(), shaderName.begin(), shaderName.end());
  bytes.push_back(static_cast<uint8_t>(permutationIndex & 0xFF));
  bytes.push_back(static_cast<uint8_t>((permutationIndex >> 8) & 0xFF));
  bytes.push_back(static_cast<uint8_t>((permutationIndex >> 16) & 0xFF));
  bytes.push_back(static_cast<uint8_t>((permutationIndex >> 24) & 0xFF));
  auto tagLen = static_cast<uint32_t>(profileTag.size());
  bytes.push_back(static_cast<uint8_t>(tagLen & 0xFF));
  bytes.push_back(static_cast<uint8_t>((tagLen >> 8) & 0xFF));
  bytes.push_back(static_cast<uint8_t>((tagLen >> 16) & 0xFF));
  bytes.push_back(static_cast<uint8_t>((tagLen >> 24) & 0xFF));
  bytes.insert(bytes.end(), profileTag.begin(), profileTag.end());
  return bytes;
}

ShaderKeyHash ComputeShaderKeyHash(const std::string& shaderName, uint32_t permutationIndex,
                                   const std::string& profileTag) {
  auto data = SerializeFieldsLE(shaderName, permutationIndex, profileTag);
  ShaderKeyHash result;
  result.hi = Fnv1a64(data, FNV1A_SEED_HI);
  result.lo = Fnv1a64(data, FNV1A_SEED_LO);
  return result;
}

ShaderKeyHash ComputeVertexKeyHash(const std::string& shaderName, uint32_t permutationIndex,
                                   const std::string& profileTag) {
  return ComputeShaderKeyHash(shaderName + "_Vert", permutationIndex, profileTag);
}

ShaderKeyHash ComputeFragmentKeyHash(const std::string& shaderName, uint32_t permutationIndex,
                                     const std::string& profileTag) {
  return ComputeShaderKeyHash(shaderName + "_Frag", permutationIndex, profileTag);
}

}  // namespace tgfx

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
//  either express or implied. See the License for the specific language governing permissions and
//  limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tgfx {

// Single source for the shader key/blob encoding shared by the runtime loader
// (PrecompiledShaderCache), the offline bundle writer (tools/shader_build_tool/BundleWriter), and
// the audit verifier (BundleVerifier). Header-only on purpose: the build tool links only
// src/gpu/shaders sources, so a .cpp here would be compiled twice or not at all on one side.
// Any change to the serialization order, the length prefixes, or the hash constants is a
// compatibility break between bundles and the runtime — see the fixed test vectors in
// test/src/ShaderKeyHashTest.cpp.

struct ShaderKeyHash {
  uint64_t hi = 0;
  uint64_t lo = 0;
};

namespace shader_key_hash_detail {

inline constexpr uint64_t FNV1A_SEED_HI = 0x6C62272E07BB0142ULL;
inline constexpr uint64_t FNV1A_SEED_LO = 0x62B821756295C58DULL;
inline constexpr uint64_t FNV1A_PRIME = 0x00000100000001B3ULL;

inline uint64_t Fnv1a64(const std::vector<uint8_t>& data, uint64_t seed) {
  uint64_t hash = seed;
  for (auto byte : data) {
    hash ^= static_cast<uint64_t>(byte);
    hash *= FNV1A_PRIME;
  }
  return hash;
}

// Serialized key layout: [nameLen:u32][name][permutationIndex:u32][tagLen:u32][tag], every length
// little-endian and measured in bytes.
inline std::vector<uint8_t> SerializeFieldsLE(const std::string& shaderName,
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

}  // namespace shader_key_hash_detail

/// Computes a 128-bit FNV-1a hash for a shader key composed of shaderName, permutationIndex, and
/// profileTag. Same implementation on the runtime and build-tool sides (see the header comment).
inline ShaderKeyHash ComputeShaderKeyHash(const std::string& shaderName, uint32_t permutationIndex,
                                          const std::string& profileTag) {
  auto data = shader_key_hash_detail::SerializeFieldsLE(shaderName, permutationIndex, profileTag);
  ShaderKeyHash result;
  result.hi = shader_key_hash_detail::Fnv1a64(data, shader_key_hash_detail::FNV1A_SEED_HI);
  result.lo = shader_key_hash_detail::Fnv1a64(data, shader_key_hash_detail::FNV1A_SEED_LO);
  return result;
}

/// Computes a stable 128-bit content hash for a code or reflection blob using the same dual-seed
/// FNV-1a scheme as the shader keys.
inline ShaderKeyHash ComputeBlobHash(const std::vector<uint8_t>& blob) {
  ShaderKeyHash result;
  result.hi = shader_key_hash_detail::Fnv1a64(blob, shader_key_hash_detail::FNV1A_SEED_HI);
  result.lo = shader_key_hash_detail::Fnv1a64(blob, shader_key_hash_detail::FNV1A_SEED_LO);
  return result;
}

/// Computes the vertex shader key hash: hash(shaderName + "_Vert", permutationIndex, profileTag).
inline ShaderKeyHash ComputeVertexKeyHash(const std::string& shaderName, uint32_t permutationIndex,
                                          const std::string& profileTag) {
  return ComputeShaderKeyHash(shaderName + "_Vert", permutationIndex, profileTag);
}

/// Computes the fragment shader key hash: hash(shaderName + "_Frag", permutationIndex, profileTag).
inline ShaderKeyHash ComputeFragmentKeyHash(const std::string& shaderName,
                                            uint32_t permutationIndex,
                                            const std::string& profileTag) {
  return ComputeShaderKeyHash(shaderName + "_Frag", permutationIndex, profileTag);
}

}  // namespace tgfx

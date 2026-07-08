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

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tgfx {

#pragma pack(push, 1)

struct BundleFileHeader {
  uint32_t magic = 0x54475346;  // "TGSF"
  uint16_t formatVersion = 1;
  uint16_t compressionType = 0;  // 0=none
  uint64_t sourceHash = 0;
  uint32_t toolchainVersion = 0x00010000;  // 1.0.0
  uint32_t entryCount = 0;
  uint32_t reflectionOffset = 0;
  uint32_t shaderDataOffset = 0;
  uint32_t shaderDataSize = 0;
  char profileTag[32] = {};
};

struct BundleIndexEntry {
  uint64_t shaderKeyHashHi = 0;
  uint64_t shaderKeyHashLo = 0;
  uint32_t vertexBlobOffset = 0;
  uint32_t vertexBlobSize = 0;
  uint32_t fragmentBlobOffset = 0;
  uint32_t fragmentBlobSize = 0;
  uint32_t reflectionOffset = 0;
};

#pragma pack(pop)

struct UniformEntry {
  std::string name;
  uint8_t format = 0;  // UniformFormat enum value
};

struct ReflectionData {
  std::vector<UniformEntry> vertexUniforms;
  std::vector<UniformEntry> fragmentUniforms;
  std::vector<UniformEntry> samplers;
};

struct VariantData {
  std::string shaderName;
  uint32_t permutationIndex = 0;
  std::string profileTag;
  std::vector<uint8_t> vertexBlob;
  std::vector<uint8_t> fragmentBlob;
  ReflectionData reflection;
};

struct ShaderKeyHash {
  uint64_t hi = 0;
  uint64_t lo = 0;
};

/// Computes a 128-bit hash for a ShaderKey (shaderName + permutationIndex + profileTag).
ShaderKeyHash ComputeShaderKeyHash(const std::string& shaderName, uint32_t permutationIndex,
                                   const std::string& profileTag);

/// Writes a complete shader bundle file containing all variant entries for one backend/profile.
bool WriteBundle(const std::string& outPath, const std::string& profileTag,
                 const std::vector<VariantData>& variants);

}  // namespace tgfx

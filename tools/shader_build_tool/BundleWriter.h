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
#include "gpu/ShaderKeyHash.h"

namespace tgfx {

struct UniformEntry {
  std::string name;
  uint8_t format = 0;      // UniformFormat enum value
  uint16_t arraySize = 1;  // std140 array element count; 1 when not an array
};

struct StageReflectionData {
  std::vector<UniformEntry> uniforms;
  std::vector<UniformEntry> samplers;
};

struct VariantData {
  std::string shaderName;
  uint32_t vertPermutationIndex = 0;
  uint32_t fragPermutationIndex = 0;
  std::string profileTag;
  std::vector<uint8_t> vertexBlob;
  std::vector<uint8_t> fragmentBlob;
  StageReflectionData vertexReflection;
  StageReflectionData fragmentReflection;
};

/// Serializes stage reflection into the bundle's on-disk reflection format:
/// [uniformCount:u8][samplerCount:u8][reserved:u8][reserved:u8] followed by the uniform entries
/// then the sampler entries, each [nameLen:u8][name:bytes][format:u8][arraySize:u16]. Exposed so
/// audit reporting can reproduce the exact byte-level reflection identity the bundle stores.
std::vector<uint8_t> SerializeStageReflection(const StageReflectionData& reflection);

/// Writes a v3 shader bundle file with separate vertex and fragment pools.
/// When compress is true, the data pool is zlib-compressed (compressionType=1 in header).
bool WriteBundle(const std::string& outPath, const std::string& profileTag,
                 const std::vector<VariantData>& variants, bool compress = false);

}  // namespace tgfx

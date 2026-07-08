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
#include <unordered_map>
#include <vector>
#include "gpu/Uniform.h"

namespace tgfx {

struct ShaderBlob {
  std::vector<uint8_t> vertexData;
  std::vector<uint8_t> fragmentData;
  std::vector<Uniform> vertexUniforms;
  std::vector<Uniform> fragmentUniforms;
  std::vector<Uniform> samplers;
};

/// Runtime cache that loads precompiled shader bundles and provides O(1) lookup by ShaderKey hash.
/// Each Context holds one instance. If no bundle is available or lookup misses, the caller falls
/// back to ProgramBuilder.
class PrecompiledShaderCache {
 public:
  /// Loads a bundle file from the given path. Returns true if the bundle was loaded successfully.
  bool loadBundle(const std::string& path);

  /// Looks up a shader variant by its 128-bit hash. Returns nullptr if not found.
  const ShaderBlob* find(uint64_t hashHi, uint64_t hashLo) const;

  /// Returns true if at least one bundle has been loaded.
  bool isLoaded() const {
    return !entries.empty();
  }

  /// Returns the number of loaded shader entries.
  size_t entryCount() const {
    return entries.size();
  }

  /// Returns the profile tag from the loaded bundle header (e.g. "vulkan-android").
  const std::string& profileTag() const {
    return _profileTag;
  }

 private:
  std::string _profileTag;
  struct HashKey {
    uint64_t hi;
    uint64_t lo;
    bool operator==(const HashKey& other) const {
      return hi == other.hi && lo == other.lo;
    }
  };

  struct HashKeyHasher {
    size_t operator()(const HashKey& k) const {
      return static_cast<size_t>(k.hi ^ (k.lo * 0x9E3779B97F4A7C15ULL));
    }
  };

  std::unordered_map<HashKey, ShaderBlob, HashKeyHasher> entries;
};

}  // namespace tgfx

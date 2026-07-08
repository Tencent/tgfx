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

struct ShaderKeyHash {
  uint64_t hi = 0;
  uint64_t lo = 0;
};

/// Computes a 128-bit FNV-1a hash for a shader key composed of shaderName, permutationIndex, and
/// profileTag. The algorithm must stay in sync with the build tool's BundleWriter.
ShaderKeyHash ComputeShaderKeyHash(const std::string& shaderName, uint32_t permutationIndex,
                                   const std::string& profileTag);

/// Computes the vertex shader key hash: hash(shaderName + "_Vert", permutationIndex, profileTag).
ShaderKeyHash ComputeVertexKeyHash(const std::string& shaderName, uint32_t permutationIndex,
                                   const std::string& profileTag);

/// Computes the fragment shader key hash: hash(shaderName + "_Frag", permutationIndex, profileTag).
ShaderKeyHash ComputeFragmentKeyHash(const std::string& shaderName, uint32_t permutationIndex,
                                     const std::string& profileTag);

}  // namespace tgfx

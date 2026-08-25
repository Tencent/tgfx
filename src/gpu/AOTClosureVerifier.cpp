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

#include "gpu/AOTClosureVerifier.h"
#include <set>
#include "gpu/ShaderKeyHash.h"
#include "gpu/shaders/PermutationRules.h"
#include "gpu/shaders/PrecompiledShader.h"

namespace tgfx {

static bool ShaderSelected(const std::string& name, const std::vector<std::string>& filter) {
  if (filter.empty()) {
    return true;
  }
  for (const auto& selected : filter) {
    if (selected == name) {
      return true;
    }
  }
  return false;
}

AOTClosureResult AOTClosureVerifier::Verify(const PrecompiledShaderCache* cache,
                                            const std::vector<std::string>& shaderNames) {
  AOTClosureResult result;
  if (cache == nullptr || !cache->isLoaded()) {
    return result;
  }
  const auto& profileTag = cache->profileTag();
  for (const auto& factory : ShaderRegistry::All()) {
    auto shader = factory();
    if (shader == nullptr) {
      continue;
    }
    auto info = shader->info();
    if (!ShaderSelected(info.name, shaderNames)) {
      continue;
    }
    // Reproduce the build tool's enumeration: the compile list is the matcher rule's reachable
    // set. Vertices are deduplicated by index because a vertex stage is compiled once per
    // vertIndex.
    auto reachable = EnumerateReachablePermutations(info.name);
    if (!reachable) {
      continue;
    }
    std::set<uint32_t> vertIndices;
    std::set<uint32_t> fragIndices;
    for (const auto& permutation : *reachable) {
      // Apply the same backend-specific exclusions as the bundle generator (RECT for non-GL,
      // FBF for WebGPU); without this the verifier reports holes the runtime can never request.
      if (!PermutationCompilesForBackend(info, permutation.first, permutation.second, profileTag)) {
        continue;
      }
      vertIndices.insert(permutation.first);
      fragIndices.insert(permutation.second);
    }
    for (auto vi : vertIndices) {
      result.expectedCount++;
      auto hash = ComputeVertexKeyHash(info.name, vi, profileTag);
      if (cache->findVertex(hash.hi, hash.lo) == nullptr) {
        result.missingCount++;
        result.missing.push_back({info.name, true, vi});
      }
    }
    for (auto fi : fragIndices) {
      result.expectedCount++;
      auto hash = ComputeFragmentKeyHash(info.name, fi, profileTag);
      if (cache->findFragment(hash.hi, hash.lo) == nullptr) {
        result.missingCount++;
        result.missing.push_back({info.name, false, fi});
      }
    }
  }
  return result;
}

}  // namespace tgfx

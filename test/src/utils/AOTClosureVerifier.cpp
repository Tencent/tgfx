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

#include "utils/AOTClosureVerifier.h"
#include <set>
#include "gpu/shaders/Families.h"
#include "gpu/shaders/FamilyDomains.h"
#include "gpu/shaders/PrecompiledShader.h"
#include "gpu/shaders/contract/ShaderBundleContract.h"
#include "utils/ReachableSet.h"

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
  // A bundle can carry a tag no profile has when it was parsed without a backend to check it
  // against. Such a bundle has no profile exclusion rules, so every reachable variant is expected.
  const auto profile = ParseProfileTag(profileTag);
  // The shell families come from the factory list; the declaration-driven families ride the
  // registry's lookup tables without a shell. Collect both into one name set and resolve each
  // through EnumerateReachablePermutations, the shared single source of the reachable sets.
  std::set<std::string> names;
  for (size_t index = 0; index < FamilyCount(); ++index) {
    names.insert(Families()[index].name);
  }
  for (const auto& name : names) {
    if (!ShaderSelected(name, shaderNames)) {
      continue;
    }
    const uint16_t familyIndex = FamilyIndexOf(name.c_str());
    if (familyIndex == 0xFFFF) {
      continue;
    }
    // Reproduce the build tool's enumeration: the compile list is the matcher rule's reachable
    // set. Vertices are deduplicated by index because a vertex stage is compiled once per
    // vertIndex.
    auto reachable = EnumerateReachablePermutations(name);
    if (!reachable) {
      continue;
    }
    std::set<uint32_t> vertIndices;
    std::set<uint32_t> fragIndices;
    for (const auto& permutation : *reachable) {
      // Apply the same profile-specific exclusions as the bundle generator (the framebuffer fetch
      // variants); without this the verifier reports holes the runtime can never request.
      if (profile && !PermutationCompilesForBackend(familyIndex, permutation.first, *profile)) {
        continue;
      }
      vertIndices.insert(permutation.first);
      fragIndices.insert(permutation.second);
    }
    for (auto vi : vertIndices) {
      result.expectedCount++;
      if (cache->findVertex(VariantIdOf(familyIndex, vi)) == nullptr) {
        result.missingCount++;
        result.missing.push_back({name, true, vi});
      }
    }
    for (auto fi : fragIndices) {
      result.expectedCount++;
      if (cache->findFragment(VariantIdOf(familyIndex, fi)) == nullptr) {
        result.missingCount++;
        result.missing.push_back({name, false, fi});
      }
    }
  }
  return result;
}

}  // namespace tgfx

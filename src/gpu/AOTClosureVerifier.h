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

#include <string>
#include <vector>
#include "gpu/PrecompiledShaderCache.h"

namespace tgfx {

/**
 * Identifies a single precompiled shader stage variant that the closure verifier expects to be
 * present in the bundle. Stage is either the vertex or fragment stage of a named shader at a
 * specific permutation index.
 */
struct AOTClosureEntry {
  std::string shaderName;
  bool isVertex = false;
  uint32_t permutationIndex = 0;
};

/**
 * Aggregated result of a closure verification pass over a set of Kernel families.
 */
struct AOTClosureResult {
  uint64_t expectedCount = 0;
  uint64_t missingCount = 0;
  std::vector<AOTClosureEntry> missing = {};

  bool isComplete() const {
    return missingCount == 0;
  }
};

/**
 * AOTClosureVerifier proves that every structural Key a Kernel family can produce is present in the
 * loaded precompiled bundle. It reproduces the build tool's enumeration exactly: for each named
 * shader it walks the vertDomain × fragDomain product, applies the same shouldCompile filter, and
 * for every surviving variant recomputes the vertex/fragment key hash and looks it up in the cache.
 * A missing lookup means the bundle does not cover a Key the runtime matcher could emit, i.e. a
 * closure hole. This is the standing definition of "the AOT path is verifiably complete" and must
 * gate any claim of zero bundle misses.
 */
class AOTClosureVerifier {
 public:
  /**
   * Verifies that all permutations of the given shader names exist in the cache. The cache must
   * already be loaded. Returns the aggregated result; callers inspect isComplete() and the missing
   * list. An empty shaderNames list verifies every registered shader.
   *
   * @param cache        The loaded precompiled shader cache to verify against.
   * @param shaderNames  Names of the shaders (Kernel families) to verify; empty means all.
   */
  static AOTClosureResult Verify(const PrecompiledShaderCache* cache,
                                 const std::vector<std::string>& shaderNames);
};

}  // namespace tgfx

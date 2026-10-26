/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions and
//  limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <optional>
#include <set>
#include <string>
#include <utility>
#include "gpu/shaders/ShaderPermutation.h"

namespace tgfx {

// The family-level rules (Inputs, Compose, Extract, the Reachable enumeration and the shared
// rejection predicates) now live in each family's own header through the ShaderFamily base; see
// ShaderFamily.h for the contract. What remains here is the registry lookup the verifier, the
// bundle manifest and the tests use to reach a family's reachable set by name.

/**
 * Returns the reachable variant set of the shader registered under the given name, or nullopt
 * when no such shader is registered.
 */
std::optional<std::set<std::pair<uint32_t, uint32_t>>> EnumerateReachablePermutations(
    const std::string& shaderName);

}  // namespace tgfx

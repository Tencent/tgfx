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

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "gpu/shaders/ShaderPermutation.h"
#include "gpu/shaders/contract/ShaderBundleContract.h"

namespace tgfx {

/**
 * Texture-read semantics a precompiled shader family implements beyond plain RGBA and alpha-only
 * samples. A family declares a capability only when its shader source applies it; MatchPermutation
 * refuses every draw that needs a capability its matched family does not declare (default deny),
 * so a family that never learned a semantic reports a miss instead of drawing wrong pixels.
 */
enum ShaderTextureRead : uint32_t {
  /// Applies the runtime read swizzle (Swizzle::ForRead) of single-plane GRAY_8 (rrra) and RG_88
  /// (rgrg) textures, through texture_read_swizzle.inc.
  ShaderTextureReadSwizzle = 1u << 0,
};

/**
 * Metadata describing a precompiled shader, including its permutation domain and source file
 * paths. The compiled variant list is defined by the matcher rules (see the family rules in
 * FamilyMatch.cpp); no shader-side filter exists.
 */
struct PrecompiledShaderInfo {
  std::string name;
  std::string vertexFile;
  std::string fragmentFile;
  PermutationDomain vertDomain;
  PermutationDomain fragDomain;
  /// ShaderTextureRead bits this family implements. Zero (the default) declares none.
  uint32_t textureReads = 0;

  /// Whether the vertex/fragment indices belong to the declared domains and agree on every
  /// mirrored dimension.
  bool IsBuildable(uint32_t vertIndex, uint32_t fragIndex) const;
};

/**
 * Returns true when the permutation is compiled into the bundle of the given profile. This is the
 * single source of truth for profile-specific variant exclusion (see
 * ProfileSupportsFramebufferFetch); both the bundle generator (shader_build_tool) and the closure
 * verifier (AOTClosureVerifier) must apply it, or the verifier reports holes the runtime can never
 * request.
 */
bool PermutationCompilesForBackend(const PrecompiledShaderInfo& info, uint32_t vertIndex,
                                   uint32_t fragIndex, ShaderProfile profile);

/**
 * The declared family's metadata, addressed by name. Every family lives in the declaration table
 * (Families.h), so there is no registration step and no dependence on static-initialization order.
 * Returns nullptr when no family carries the name.
 */
const PrecompiledShaderInfo* FindShaderInfo(const std::string& name);

}  // namespace tgfx

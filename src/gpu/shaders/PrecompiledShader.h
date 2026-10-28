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
#include "gpu/ShaderProfile.h"
#include "gpu/shaders/ShaderPermutation.h"

namespace tgfx {

class BindingPlanBuilder;
class ProgramInfo;

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
 * paths. The compiled variant list is defined by the matcher rules (see the family rules in ShaderFamily.h);
 * no shader-side filter exists.
 */
struct PrecompiledShaderInfo {
  std::string name;
  std::string vertexFile;
  std::string fragmentFile;
  PermutationDomain vertDomain;
  PermutationDomain fragDomain;
  std::string gpClassName;
  /// ShaderTextureRead bits this family implements. Zero (the default) declares none.
  uint32_t textureReads = 0;
};

/**
 * Returns true when the vertex/fragment indices belong to the shader's declared domains and
 * agree on every mirrored dimension.
 */
bool IsBuildablePermutation(const PrecompiledShaderInfo& info, uint32_t vertIndex,
                            uint32_t fragIndex);

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
 * Returns the reachable variant set of the shader registered under the given name, or nullopt
 * when no such shader is registered. The family-level rules (Inputs, Compose, Extract, the
 * Reachable enumeration and the shared rejection predicates) live in each family's own header
 * through the ShaderFamily base; see ShaderFamily.h for the contract.
 */
std::optional<std::set<std::pair<uint32_t, uint32_t>>> EnumerateReachablePermutations(
    const std::string& shaderName);

/**
 * Base class for all precompiled shader declarations. Each concrete shader must override info()
 * to provide its metadata.
 */
class PrecompiledShader {
 public:
  virtual ~PrecompiledShader() = default;

  virtual PrecompiledShaderInfo info() const = 0;

  /**
   * Declares the parameter module instances this kernel binds for a draw the family matched:
   * the builder records each instance's processor and field mask, and the program creation
   * resolves the fields against the fragment block and attaches the plan to the program. A
   * migrated family returns true; the default (un-migrated) keeps the processors' onSetData
   * writes exactly as before.
   */
  virtual bool planBindings(const ProgramInfo*, BindingPlanBuilder*) const {
    return false;
  }
};

/**
 * Global registry of all precompiled shader factories. Shader families register themselves at
 * static initialization time via the TGFX_REGISTER_SHADER_FAMILY macro (see ShaderFamily.h).
 */
class ShaderRegistry {
 public:
  using Factory = std::unique_ptr<PrecompiledShader> (*)();
  using Enumerator = std::set<std::pair<uint32_t, uint32_t>> (*)();

  /** Identical repeated declarations are allowed; conflicting declarations abort registration. */
  static void Register(Factory factory, Enumerator enumerator);

  static const std::vector<Factory>& All();

  /** Returns the registered metadata for a shader name, or nullptr when no shader is registered. */
  static const PrecompiledShaderInfo* Find(const std::string& name);

  /** Invokes the family's registered enumerator, or returns nullopt for an unknown name. */
  static std::optional<std::set<std::pair<uint32_t, uint32_t>>> Enumerate(const std::string& name);
};

// Internal helper macros for unique name generation.
#define TGFX_CONCAT_IMPL(a, b) a##b
#define TGFX_CONCAT(a, b) TGFX_CONCAT_IMPL(a, b)

}  // namespace tgfx

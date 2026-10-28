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

#include <memory>
#include <optional>
#include <set>
#include <type_traits>
#include <utility>
#include <vector>
#include "gpu/shaders/PrecompiledShader.h"

namespace tgfx {

class ProgramInfo;

/**
 * Detection traits for the optional members a ShaderFamily subclass may declare. A family that
 * does not declare a member keeps the documented default instead of failing to compile, so a
 * minimal family only carries the mandatory pieces (Name, VertFile, FragFile, VD, FD,
 * ServesAlphaOnly, Inputs, Compose, Extract).
 */
template <typename T, typename = void>
struct FamilyHasGpClassName : std::false_type {};
template <typename T>
struct FamilyHasGpClassName<T, std::void_t<decltype(T::GpClassName)>> : std::true_type {};

template <typename T, typename = void>
struct FamilyHasTextureReads : std::false_type {};
template <typename T>
struct FamilyHasTextureReads<T, std::void_t<decltype(T::TextureReads)>> : std::true_type {};

// A family migrated to module parameter binding declares a static BindingFor. The detection is
// instantiated only in runtime translation units (the program creator's X-macro dispatch); the
// tool compiles the family headers but never links the family .cpps, so nothing here may
// reference the definition.
template <typename T, typename = void>
struct FamilyHasBindingFor : std::false_type {};
template <typename T>
struct FamilyHasBindingFor<T, std::void_t<decltype(T::BindingFor(nullptr, nullptr))>>
    : std::true_type {};

template <typename T, typename = void>
struct FamilyHasAllInputs : std::false_type {};
template <typename T>
struct FamilyHasAllInputs<T, std::void_t<decltype(T::AllInputs())>> : std::true_type {};

template <typename T, typename = void>
struct FamilyHasFragValuesAllowed : std::false_type {};
template <typename T>
struct FamilyHasFragValuesAllowed<
    T, std::void_t<decltype(T::FragValuesAllowed(std::declval<std::vector<int>>()))>>
    : std::true_type {};

/**
 * The factory the registration macro installs for a family. A free function template has one
 * address across translation units (ODR-merged), so re-registration from every include site is
 * an exact duplicate the registry accepts.
 */
template <typename T>
std::unique_ptr<PrecompiledShader> CreateShaderFamilyInstance() {
  return std::make_unique<T>();
}

/**
 * Self-describing precompiled shader family. A subclass declares its identity (Name, VertFile,
 * FragFile, and optionally GpClassName / TextureReads), its permutation dimensions (VD / FD),
 * whether alpha-only targets are served (ServesAlphaOnly), and the matching contract:
 *
 *   struct Inputs              — everything the runtime needs to pick a variant;
 *   static Compose(Inputs)     — pure mapping from inputs to dimension values, inline in the
 *                                header (shader_build_tool links it to enumerate the bundle);
 *   static Extract(ProgramInfo*) — whole-draw guards, defined in the family's .cpp.
 *
 * Match (Extract → Compose → encode) and Reachable (Compose over the input space, or the
 * mirrored domain product for a pass-through family) both route through the same Compose, so
 * the compiled variant list and the runtime matcher cannot drift apart. The mirrored default
 * optionally applies the family's FragValuesAllowed predicate to keep unreachable combinations
 * out of the bundle.
 */
template <typename T>
class ShaderFamily : public PrecompiledShader {
 public:
  PrecompiledShaderInfo info() const override {
    PrecompiledShaderInfo result = {T::Name,         T::VertFile,     T::FragFile,
                                    T::VD::domain(), T::FD::domain(), ""};
    if constexpr (FamilyHasGpClassName<T>::value) {
      result.gpClassName = T::GpClassName;
    }
    if constexpr (FamilyHasTextureReads<T>::value) {
      result.textureReads = T::TextureReads;
    }
    return result;
  }

  /**
   * Runtime matching: narrows the draw to Inputs, maps them through Compose, and encodes the
   * dimension values into variant indices. The dispatch X-macro keeps calling Family::Match, so
   * migration only changes what the family itself declares.
   */
  static std::optional<PermutationVariant> Match(const ProgramInfo* programInfo) {
    const auto inputs = T::Extract(programInfo);
    if (!inputs) {
      return std::nullopt;
    }
    return ComposeToVariant(*inputs);
  }

  /**
   * The reachable variant set, registered as the family's bundle enumerator. A family with
   * AllInputs maps every input through Compose; a pass-through family (no AllInputs) takes the
   * mirrored domain product, filtered by FragValuesAllowed when declared.
   */
  static std::set<std::pair<uint32_t, uint32_t>> Reachable() {
    if constexpr (FamilyHasAllInputs<T>::value) {
      std::set<std::pair<uint32_t, uint32_t>> result;
      for (const auto& inputs : T::AllInputs()) {
        const auto variant = ComposeToVariant(inputs);
        if (variant) {
          result.insert({variant->vertPermutationIndex, variant->fragPermutationIndex});
        }
      }
      return result;
    } else {
      return MirroredReachable();
    }
  }

#ifndef NDEBUG
  /**
   * The reachable set computed once per process, for the dispatch-time membership audit: a Match
   * result outside the set means Extract produces an input combination AllInputs does not
   * enumerate, so the bundle lacks the variant and the draw would be dropped at runtime.
   */
  static const std::set<std::pair<uint32_t, uint32_t>>& CachedReachable() {
    static const auto set = T::Reachable();
    return set;
  }
#endif

 private:
  // A function template, not a plain member: the base is instantiated while T is still
  // incomplete (CRTP), so a signature naming T::Inputs would not compile.
  template <typename Inputs>
  static std::optional<PermutationVariant> ComposeToVariant(const Inputs& inputs) {
    const auto composed = T::Compose(inputs);
    if (!composed) {
      return std::nullopt;
    }
    return PermutationVariant{T::VD::domain().encode(composed->vertValues),
                              T::FD::domain().encode(composed->fragValues)};
  }

  static std::set<std::pair<uint32_t, uint32_t>> MirroredReachable() {
    const auto vertDomain = T::VD::domain();
    const auto fragDomain = T::FD::domain();
    std::set<std::pair<uint32_t, uint32_t>> result;
    for (uint32_t vertIndex = 0; vertIndex < vertDomain.totalCount(); ++vertIndex) {
      const auto vertValues = vertDomain.decode(vertIndex);
      for (uint32_t fragIndex = 0; fragIndex < fragDomain.totalCount(); ++fragIndex) {
        const auto fragValues = fragDomain.decode(fragIndex);
        if (!MirroredDimsAgree(vertDomain, fragDomain, vertValues, fragValues)) {
          continue;
        }
        if constexpr (FamilyHasFragValuesAllowed<T>::value) {
          if (!T::FragValuesAllowed(fragValues)) {
            continue;
          }
        }
        result.insert({vertIndex, fragIndex});
      }
    }
    return result;
  }
};

// Registers the family factory and its Reachable set during static initialization. Both are
// stable free-function / static-member addresses, so every translation unit that includes the
// family header registers an exact duplicate.
#define TGFX_REGISTER_SHADER_FAMILY_IMPL(ShaderClass, counter)                           \
  namespace {                                                                            \
  struct TGFX_CONCAT(ShaderFamilyAutoReg_, counter) {                                    \
    TGFX_CONCAT(ShaderFamilyAutoReg_, counter)() {                                       \
      ::tgfx::ShaderRegistry::Register(&::tgfx::CreateShaderFamilyInstance<ShaderClass>, \
                                       &ShaderClass::Reachable);                         \
    }                                                                                    \
  };                                                                                     \
  static TGFX_CONCAT(ShaderFamilyAutoReg_, counter)                                      \
      TGFX_CONCAT(shaderFamilyAutoRegInstance_, counter){};                              \
  }

/**
 * Registers a ShaderFamily subclass. Place after the class definition, outside the namespace:
 *   TGFX_REGISTER_SHADER_FAMILY(tgfx::MyFillShader)
 */
#define TGFX_REGISTER_SHADER_FAMILY(ShaderClass) \
  TGFX_REGISTER_SHADER_FAMILY_IMPL(ShaderClass, __COUNTER__)

}  // namespace tgfx

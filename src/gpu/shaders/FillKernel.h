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
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cassert>
#include <cstring>
#include <optional>
#include <utility>
#include <vector>
#include "gpu/aot/DrawShape.h"
#include "gpu/shaders/ShaderFamily.h"

namespace tgfx {

/**
 * One variant dimension a FillKernel family declares: the GLSL #define name and its value count.
 * An array's order is the bundle's dimension order, so it is part of every artifact key:
 * reordering an existing family's dims renumbers every variant and invalidates the bundle.
 */
struct Dim {
  const char* name;
  int count;
};

class Values;

/**
 * The dimension values a family's Select assigns, addressed by name. A family builds one with
 * vert()/frag() and returns it; Compose encodes it against the declared dims, so a Select that
 * names an undeclared dimension, leaves one unassigned, or writes a value outside its declared
 * range fails an assert instead of silently producing a wrong variant.
 */
class Variants {
 public:
  struct Entry {
    const char* name;
    int value;
    bool isVert;
  };

  Variants& vert(const char* name, int value) {
    entries.push_back({name, value, true});
    return *this;
  }

  Variants& frag(const char* name, int value) {
    entries.push_back({name, value, false});
    return *this;
  }

  const std::vector<Entry>& getEntries() const {
    return entries;
  }

  /**
   * The fragment-stage view of the assigned values: the same predicate view FragValuesAllowed
   * receives from the mirrored enumeration, so an `allowed` predicate sees one consistent set of
   * dimensions from both call sites.
   */
  Values fragView() const;

 private:
  std::vector<Entry> entries;
};

inline const char* DimensionName(const PermutationDimension& dimension) {
  if (auto* value = std::get_if<PermutationBool>(&dimension)) {
    return value->defineName;
  }
  if (auto* value = std::get_if<PermutationEnum>(&dimension)) {
    return value->defineName;
  }
  if (auto* value = std::get_if<PermutationInt>(&dimension)) {
    return value->defineName;
  }
  return "";
}

/**
 * A read-only name -> value view over one stage's dimension values. It has two sources: the
 * Variants a family's Select built (Compose), and a decoded positional vector (the mirrored
 * Reachable enumeration feeding FragValuesAllowed). A name the view does not carry reads as 0.
 */
class Values {
 public:
  Values() = default;

  void add(const char* name, int value) {
    entries.emplace_back(name, value);
  }

  explicit Values(const Variants& variants) {
    for (const auto& entry : variants.getEntries()) {
      entries.emplace_back(entry.name, entry.value);
    }
  }

  Values(const std::vector<int>& positional, const PermutationDomain& domain) {
    const auto& dimensions = domain.getDimensions();
    for (size_t i = 0; i < dimensions.size() && i < positional.size(); ++i) {
      entries.emplace_back(DimensionName(dimensions[i]), positional[i]);
    }
  }

  int operator[](const char* name) const {
    for (const auto& entry : entries) {
      if (std::strcmp(entry.first, name) == 0) {
        return entry.second;
      }
    }
    return 0;
  }

 private:
  std::vector<std::pair<const char*, int>> entries;
};

inline Values Variants::fragView() const {
  Values result;
  for (const auto& entry : entries) {
    if (!entry.isVert) {
      result.add(entry.name, entry.value);
    }
  }
  return result;
}

template <typename T, typename = void>
struct FamilyHasVertDims : std::false_type {};
template <typename T>
struct FamilyHasVertDims<T, std::void_t<decltype(T::vertDims)>> : std::true_type {};

template <typename T, typename = void>
struct FamilyHasFragDims : std::false_type {};
template <typename T>
struct FamilyHasFragDims<T, std::void_t<decltype(T::fragDims)>> : std::true_type {};

template <typename T, typename = void>
struct FamilyHasAllowed : std::false_type {};
template <typename T>
struct FamilyHasAllowed<T, std::void_t<decltype(T::allowed(std::declval<const Values&>()))>>
    : std::true_type {};

template <size_t N>
PermutationDomain BuildDomain(const Dim (&dims)[N]) {
  std::vector<PermutationDimension> dimensions = {};
  dimensions.reserve(N);
  for (size_t i = 0; i < N; ++i) {
    dimensions.push_back(PermutationInt(dims[i].name, dims[i].count));
  }
  return PermutationDomain(std::move(dimensions));
}

inline bool EncodeOne(const Variants& variants, const PermutationDomain& domain, bool isVert,
                      std::vector<int>* values) {
  values->assign(domain.dimensionCount(), 0);
  std::vector<uint8_t> assigned(domain.dimensionCount(), 0);
  for (const auto& entry : variants.getEntries()) {
    if (entry.isVert != isVert) {
      continue;
    }
    auto position = domain.indexOf(entry.name);
    if (position >= domain.dimensionCount()) {
      assert(false && "unknown dimension name: not in the family's dims array");
      return false;
    }
    if (entry.value < 0 || entry.value >= domain.valueCountOf(position)) {
      assert(false && "dimension value outside its declared range");
      return false;
    }
    (*values)[position] = entry.value;
    assigned[position] = 1;
  }
  for (size_t i = 0; i < assigned.size(); ++i) {
    if (assigned[i] == 0) {
      assert(false && "Compose finished with a declared dimension never assigned");
      return false;
    }
  }
  return true;
}

inline std::optional<RuleComposedValues> EncodeVariants(const Variants& variants,
                                                        const PermutationDomain& vertDomain,
                                                        const PermutationDomain& fragDomain) {
  RuleComposedValues composed = {};
  if (!EncodeOne(variants, vertDomain, true, &composed.vertValues)) {
    return std::nullopt;
  }
  if (!EncodeOne(variants, fragDomain, false, &composed.fragValues)) {
    return std::nullopt;
  }
  return composed;
}

template <typename T>
struct FillVertDims {
  static PermutationDomain domain() {
    if constexpr (FamilyHasVertDims<T>::value) {
      return BuildDomain(T::vertDims);
    } else {
      return PermutationDomain({});
    }
  }
};

template <typename T>
struct FillFragDims {
  static PermutationDomain domain() {
    if constexpr (FamilyHasFragDims<T>::value) {
      return BuildDomain(T::fragDims);
    } else {
      return PermutationDomain({});
    }
  }
};

/**
 * A family whose variants are a cartesian product over its declared dims. The family answers two
 * questions — Accept (can I draw this shape) and Select (which variant) — and this base derives
 * everything ShaderFamily registers: info(), the matcher entry, the Compose that both the
 * runtime match and the bundle enumeration share, and the reachable set.
 *
 * The family may additionally declare `static bool allowed(const Values&)` to exclude
 * combinations the cartesian product would otherwise include; the predicate is consulted by both
 * Compose and the reachable enumeration, so the compiled set and the matcher stay in lockstep.
 *
 * The VD/FD aliases bridge ShaderFamily's T::VD::domain() / T::FD::domain() dependency onto the
 * family's dims arrays; without them MirroredReachable would not compile.
 */
template <typename T>
class FillKernel : public ShaderFamily<T> {
 public:
  using VD = FillVertDims<T>;
  using FD = FillFragDims<T>;

  static std::optional<DrawShape> ExtractFacts(const DrawShape& shape, PermutationMatchFailure*) {
    if (!T::Accept(shape)) {
      return std::nullopt;
    }
    return shape;
  }

  static std::optional<RuleComposedValues> Compose(const DrawShape& shape) {
    if (!T::Accept(shape)) {
      return std::nullopt;
    }
    auto variants = T::Select(shape);
    if constexpr (FamilyHasAllowed<T>::value) {
      if (!T::allowed(variants.fragView())) {
        return std::nullopt;
      }
    }
    return EncodeVariants(variants, VD::domain(), FD::domain());
  }

  /**
   * ShaderFamily's mirrored enumeration only consults this name, not `allowed`, so the base
   * defines it unconditionally and forwards to the family's predicate when one exists. A family
   * that declares only `allowed` would otherwise keep its unreachable combinations in the bundle.
   */
  static bool FragValuesAllowed(const std::vector<int>& fragValues) {
    if constexpr (FamilyHasAllowed<T>::value) {
      return T::allowed(Values(fragValues, FD::domain()));
    } else {
      static_cast<void>(fragValues);
      return true;
    }
  }

  /**
   * ShaderFamily::Match calls T::Extract; the production dispatch reaches dedicated families
   * through the executor table instead, but keeping Extract compilable avoids a latent break if
   * a route ever calls Match on a dedicated family.
   */
  static std::optional<DrawShape> Extract(const ProgramInfo* programInfo,
                                          PermutationMatchFailure* failure) {
    return ExtractFacts(ExtractDrawShape(programInfo), failure);
  }
};

}  // namespace tgfx

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
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. See the License for the specific language governing permissions and
//  limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>
#include "gpu/shaders/Module.h"

namespace tgfx {

inline constexpr size_t MaxDims = 6;
inline constexpr size_t MaxModules = 8;

/// One module slot of a family's body. A slot with a pinned array fixes dims to single values
/// inside this family (per-dim of the module: -1 = the module's full range, >= 0 = that value);
/// a plain module pointer keeps every dim free.
struct FamilyModule {
  const Module* module = nullptr;
  const int8_t* pinned = nullptr;

  constexpr FamilyModule(const Module* m) : module(m), pinned(nullptr) {
  }
  constexpr FamilyModule(const Module* m, const int8_t* p) : module(m), pinned(p) {
  }
};

/// One program template plus every dim value. The values array is flattened over the module
/// sequence [geometry, body modules..., Xfer] (Frame carries no dims); DimOffsetOf gives the
/// flattened index of a module's first dim.
struct Variant {
  uint16_t family = 0;
  uint8_t geometry = 0;
  std::array<uint8_t, MaxDims> values = {};
  /// The mixed-radix ordinal inside the family, over (geometry, the free dims in module order);
  /// a pinned dim occupies no bits. EnumerateVariants assigns it.
  uint32_t ordinal = 0;

  /// family index << 20 | ordinal.
  uint32_t id() const {
    return (static_cast<uint32_t>(family) << 20) | ordinal;
  }
};

/// The family a draw matches: one geometry module per program template and the body modules, in
/// order. Frame rides first and Xfer last implicitly (see ModulesOf); the family never lists them.
struct Family {
  const char* name = "";
  const Module* const* geometries = nullptr;
  uint8_t geometryCount = 0;
  const FamilyModule* modules = nullptr;
  uint8_t moduleCount = 0;
  /// Optional product filter over enumerated variants (e.g. the textured-stroke exclusion).
  bool (*allowed)(const Variant& variant, const Family& family) = nullptr;
  /// The last body module owns the fragment output (the legacy chain's interpreter mixes the
  /// XferProcessor's blend pass inside its own evaluation): the assembler then declares the Xfer
  /// module's interface but skips its fragment hook, so the composition runs exactly once.
  bool bodyOwnsOutput = false;
};

/// The module sequence of one variant: [Frame, geometry, body modules..., Xfer]. A module whose
/// PRESENT dim is 0 is absent: it contributes no elements and the assembler skips its hooks, but
/// it still occupies its slot (and its dims keep their flattened positions).
struct VariantModules {
  const Module* list[MaxModules] = {};
  uint8_t count = 0;
};

/// The flattened index of the module at slotIndex's first dim (the geometry is slot 1, after
/// Frame). Returns 0 for Frame and slots without dims.
uint8_t DimOffsetOf(const Family& family, const Variant& variant, uint8_t slotIndex);

/// The module at slotIndex, or nullptr when slotIndex is past the sequence.
const Module* ModuleAt(const Family& family, const Variant& variant, uint8_t slotIndex);

/// The value of the dim at dimIndex of the module at slotIndex.
uint8_t DimValueOf(const Family& family, const Variant& variant, uint8_t slotIndex,
                   uint8_t dimIndex);

/// Whether the module at slotIndex is present (no PRESENT dim, or its value is not 0).
bool ModulePresent(const Family& family, const Variant& variant, uint8_t slotIndex);

/// The module sequence of the variant ([Frame, geometry, body..., Xfer]).
VariantModules ModulesOf(const Family& family, const Variant& variant);

/// Whether the element exists in the variant (dimBase is its slot's DimOffsetOf); the pure form
/// taking the dim value directly lives in Module.h.
inline bool ElementPresentInVariant(const Element& element, uint8_t dimBase,
                                    const Variant& variant) {
  return ElementPresentInVariant(
      element, element.when < 0 ? 0 : variant.values[dimBase + static_cast<uint8_t>(element.when)]);
}

/// The element's array count in the variant (dimBase is its slot's DimOffsetOf); the pure form
/// taking the dim value directly lives in Module.h.
inline uint16_t ElementCountOf(const Element& element, uint8_t dimBase, const Variant& variant) {
  return ElementCountOf(element,
                        element.countWhen < 0
                            ? 0
                            : variant.values[dimBase + static_cast<uint8_t>(element.countWhen)]);
}

/// The fixed family table (Families.h); its order is the family index.
const Family* Families();
size_t FamilyCount();

/// Enumerates the family's variants: the geometry choice crossed with every free dim (pinned
/// single-value dims do not iterate), filtered by the family's allowed predicate when set.
std::vector<Variant> EnumerateVariants(const Family& family, uint16_t familyIndex);

/// Builds the family's variant from its geometry choice and free dim values, in flattened
/// declaration order (geometry first, then body modules, Xfer last; pinned dims occupy no
/// position). The pinned values come from the declaration and the ordinal is composed by the same
/// radix rule EnumerateVariants uses, so no match rule hand-rolls its own encoding. Nullopt when
/// the geometry or a value is out of range, the count disagrees with the declaration, or the
/// family's allowed filter rejects the result — the draw then stays unmatched instead of
/// producing a variant outside the reachable set.
std::optional<Variant> EncodeVariant(uint16_t familyIndex, uint8_t geometry,
                                     const std::vector<uint8_t>& freeValues);

/// The inverse of EncodeVariant: rebuilds the full variant (pinned values included) from the
/// family index and the ordinal. Nullopt when the ordinal exceeds the family's variant space or
/// the allowed filter rejects it.
std::optional<Variant> DecodeVariant(uint16_t familyIndex, uint32_t ordinal);

}  // namespace tgfx

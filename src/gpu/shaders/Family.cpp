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

#include "gpu/shaders/Family.h"
#include <cassert>
#include <cstring>
#include <optional>
#include "gpu/shaders/Families.h"
#include "gpu/shaders/modules/Frame.h"
#include "gpu/shaders/modules/Xfer.h"

namespace tgfx {

namespace {

struct FreeDim {
  uint8_t slot;
  uint8_t dim;
  uint8_t iterations;
};

/// The number of values a dim iterates inside one family: 1 when pinned, else the module's count.
uint8_t DimIterations(const Module& module, const int8_t* pinned, uint8_t dimIndex) {
  if (pinned != nullptr && pinned[dimIndex] >= 0) {
    return 1;
  }
  return module.dims[dimIndex].count;
}

/// The value a dim takes at iteration step: the pinned value when fixed, else the step itself.
uint8_t DimValueAt(const int8_t* pinned, uint8_t dimIndex, uint8_t step) {
  if (pinned != nullptr && pinned[dimIndex] >= 0) {
    return static_cast<uint8_t>(pinned[dimIndex]);
  }
  return step;
}

/// Records one slot's dims into the seed variant's flattened values and appends its free dims.
/// flatIndex is the running flattened dim position; slot is the module slot the dims belong to.
void CollectSlotDims(const Module& module, const int8_t* pinned, uint8_t slot, Variant* seed,
                     uint8_t* flatIndex, std::vector<FreeDim>* freeDims) {
  for (uint8_t dim = 0; dim < module.dimCount; ++dim, ++*flatIndex) {
    seed->values[*flatIndex] = DimValueAt(pinned, dim, 0);
    const uint8_t iterations = DimIterations(module, pinned, dim);
    if (iterations > 1) {
      FreeDim free = {slot, dim, iterations};
      freeDims->push_back(free);
    }
  }
}

/// The seed variant of one geometry — every dim at its first-step value (the pinned dims at their
/// declared value, the free dims at 0) — plus the family's free-dim list. EnumerateVariants,
/// EncodeVariant and DecodeVariant all build their variants through it, so the flattened layout
/// and the pinning exist in exactly one place.
Variant BuildFamilySeed(const Family& family, uint16_t familyIndex, uint8_t geometry,
                        std::vector<FreeDim>* freeDims) {
  freeDims->clear();
  Variant seed = {};
  seed.family = familyIndex;
  seed.geometry = geometry;
  uint8_t flatIndex = 0;
  CollectSlotDims(*family.geometries[geometry], nullptr, 1, &seed, &flatIndex, freeDims);
  for (uint8_t index = 0; index < family.moduleCount; ++index) {
    CollectSlotDims(*family.modules[index].module, family.modules[index].pinned,
                    static_cast<uint8_t>(index + 2), &seed, &flatIndex, freeDims);
  }
  CollectSlotDims(XferModule(), nullptr, static_cast<uint8_t>(family.moduleCount + 2), &seed,
                  &flatIndex, freeDims);
  return seed;
}

/// The variant's ordinal: the geometry is the lowest radix (the family's geometry count), then
/// the free dims in flattened order. The one definition of the rule — EnumerateVariants,
/// EncodeVariant and DecodeVariant all compose through it, so a hand-rolled formula elsewhere
/// cannot drift from the enumeration.
uint32_t ComposeVariantOrdinal(const Family& family, const Variant& variant,
                               const std::vector<FreeDim>& freeDims) {
  uint32_t ordinal = variant.geometry;
  uint32_t stride = family.geometryCount;
  for (const auto& free : freeDims) {
    const uint32_t value = variant.values[DimOffsetOf(family, variant, free.slot) + free.dim];
    ordinal += value * stride;
    stride *= free.iterations;
  }
  return ordinal;
}

}  // namespace

uint8_t DimOffsetOf(const Family& family, const Variant& variant, uint8_t slotIndex) {
  const auto modules = ModulesOf(family, variant);
  uint8_t offset = 0;
  for (uint8_t slot = 1; slot < slotIndex && slot < modules.count; ++slot) {
    offset = static_cast<uint8_t>(offset + modules.list[slot]->dimCount);
  }
  return offset;
}

const Module* ModuleAt(const Family& family, const Variant& variant, uint8_t slotIndex) {
  const auto modules = ModulesOf(family, variant);
  if (slotIndex >= modules.count) {
    return nullptr;
  }
  return modules.list[slotIndex];
}

uint8_t DimValueOf(const Family& family, const Variant& variant, uint8_t slotIndex,
                   uint8_t dimIndex) {
  const auto* module = ModuleAt(family, variant, slotIndex);
  if (module == nullptr || dimIndex >= module->dimCount) {
    return 0;
  }
  return variant.values[DimOffsetOf(family, variant, slotIndex) + dimIndex];
}

bool ModulePresent(const Family& family, const Variant& variant, uint8_t slotIndex) {
  const auto* module = ModuleAt(family, variant, slotIndex);
  if (module == nullptr) {
    return false;
  }
  for (uint8_t dim = 0; dim < module->dimCount; ++dim) {
    if (std::strcmp(module->dims[dim].name, "PRESENT") == 0) {
      return DimValueOf(family, variant, slotIndex, dim) != 0;
    }
  }
  return true;
}

VariantModules ModulesOf(const Family& family, const Variant& variant) {
  VariantModules modules = {};
  modules.list[modules.count++] = &FrameModule();
  if (variant.geometry < family.geometryCount) {
    modules.list[modules.count++] = family.geometries[variant.geometry];
  }
  for (uint8_t index = 0; index < family.moduleCount; ++index) {
    if (modules.count >= MaxModules - 1) {
      break;
    }
    modules.list[modules.count++] = family.modules[index].module;
  }
  modules.list[modules.count++] = &XferModule();
  return modules;
}

const Family* Families() {
  return FamilyTable();
}

size_t FamilyCount() {
  return FamilyTableCount();
}

/// The RRect family's product rule: the textured form never combines with stroke (the former
/// NonAARRectFillShader's `allowed` predicate, carried over).
bool RRectVariantAllowed(const Variant& variant, const Family& family) {
  // Slot layout: Frame(0), RRectGeometry(1), TiledTexture(2), AnalyticClip(3), Xfer(4).
  const uint8_t stroke = DimValueOf(family, variant, 1, 0);
  const uint8_t present = DimValueOf(family, variant, 2, 1);
  return !(present != 0 && stroke != 0);
}

/// The ShapeInstanced family's product rule: VERTEX_COLOR only reserves the geometry's
/// stride-filler color slot, so the variant where it is present is never a real program.
bool ShapeInstancedVariantAllowed(const Variant& variant, const Family&) {
  return variant.values[0] == 0;
}

/// The ShapeInstancedTextureCoverage family's product rule: VERTEX_COLOR only reserves the
/// geometry's stride-filler coverage slot, so the variant where it is present is never a real
/// program (the same hole ShapeInstanced carries); and the legacy kernel served the no-xfer case
/// only, so the blended xfer kinds stay out of the bundle.
bool ShapeInstancedTextureCoverageVariantAllowed(const Variant& variant, const Family& family) {
  // Slot layout: Frame(0), ShapeInstancedGeometry(1), Gradient(2), CoverageTexture(3),
  // AnalyticClip(4), Xfer(5).
  const uint8_t vertexColor = DimValueOf(family, variant, 1, 0);
  const uint8_t hasXp = DimValueOf(family, variant, 5, 0);
  return vertexColor == 0 && hasXp == 0;
}

/// The QuadTexture family's product rule: the geometry and the Texture module both spell
/// HAS_SUBSET, so the two radices must move together.
bool QuadTextureVariantAllowed(const Variant& variant, const Family& family) {
  const uint8_t geometrySubset = DimValueOf(family, variant, 1, 1);
  const uint8_t textureSubset = DimValueOf(family, variant, 2, 0);
  return geometrySubset == textureSubset;
}

/// The YUVTexture family's product rule: the YUV path never carries the geometry's subset
/// attribute, so only its HAS_SUBSET = 0 half is real.
bool YUVTextureVariantAllowed(const Variant& variant, const Family& family) {
  return DimValueOf(family, variant, 1, 1) == 0;
}

std::vector<Variant> EnumerateVariants(const Family& family, uint16_t familyIndex) {
  std::vector<Variant> variants;
  std::vector<FreeDim> freeDims;
  for (uint8_t geometry = 0; geometry < family.geometryCount; ++geometry) {
    const Variant seed = BuildFamilySeed(family, familyIndex, geometry, &freeDims);
    size_t total = 1;
    for (const auto& free : freeDims) {
      total *= free.iterations;
    }
    for (size_t step = 0; step < total; ++step) {
      Variant variant = seed;
      size_t remainder = step;
      for (const auto& free : freeDims) {
        const uint8_t value = static_cast<uint8_t>(remainder % free.iterations);
        variant.values[DimOffsetOf(family, variant, free.slot) + free.dim] = value;
        remainder /= free.iterations;
      }
      variant.ordinal = ComposeVariantOrdinal(family, variant, freeDims);
      if (family.allowed == nullptr || family.allowed(variant, family)) {
        variants.push_back(variant);
      }
    }
  }
  return variants;
}

std::optional<Variant> EncodeVariant(uint16_t familyIndex, uint8_t geometry,
                                     const std::vector<uint8_t>& freeValues) {
  if (familyIndex >= FamilyCount()) {
    return std::nullopt;
  }
  const Family& family = Families()[familyIndex];
  if (geometry >= family.geometryCount) {
    return std::nullopt;
  }
  std::vector<FreeDim> freeDims;
  Variant variant = BuildFamilySeed(family, familyIndex, geometry, &freeDims);
  // A count disagreement means the declaration changed under a match rule — a bug to fix at the
  // call site, not a draw shape to reject. The plain assert (not the project's DEBUG_ASSERT)
  // keeps this file inside the tool-side declarations library, which carries no tgfx includes.
  assert(freeValues.size() == freeDims.size());
  if (freeValues.size() != freeDims.size()) {
    return std::nullopt;
  }
  for (size_t index = 0; index < freeDims.size(); ++index) {
    const FreeDim& free = freeDims[index];
    const uint8_t value = freeValues[index];
    assert(value < free.iterations);
    if (value >= free.iterations) {
      return std::nullopt;
    }
    variant.values[DimOffsetOf(family, variant, free.slot) + free.dim] = value;
  }
  variant.ordinal = ComposeVariantOrdinal(family, variant, freeDims);
  if (family.allowed != nullptr && !family.allowed(variant, family)) {
    return std::nullopt;
  }
  return variant;
}

std::optional<Variant> DecodeVariant(uint16_t familyIndex, uint32_t ordinal) {
  if (familyIndex >= FamilyCount()) {
    return std::nullopt;
  }
  const Family& family = Families()[familyIndex];
  if (family.geometryCount == 0) {
    return std::nullopt;
  }
  uint32_t remainder = ordinal;
  const uint8_t geometry = static_cast<uint8_t>(remainder % family.geometryCount);
  remainder /= family.geometryCount;
  std::vector<FreeDim> freeDims;
  Variant variant = BuildFamilySeed(family, familyIndex, geometry, &freeDims);
  for (const auto& free : freeDims) {
    variant.values[DimOffsetOf(family, variant, free.slot) + free.dim] =
        static_cast<uint8_t>(remainder % free.iterations);
    remainder /= free.iterations;
  }
  if (remainder != 0) {
    // The ordinal exceeds the family's variant space.
    return std::nullopt;
  }
  variant.ordinal = ComposeVariantOrdinal(family, variant, freeDims);
  assert(variant.ordinal == ordinal);
  if (family.allowed != nullptr && !family.allowed(variant, family)) {
    return std::nullopt;
  }
  return variant;
}

}  // namespace tgfx

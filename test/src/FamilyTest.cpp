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
//  either express or implied. see the License for the specific language governing permissions and
//  limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////
// The EncodeVariant/DecodeVariant contract: every variant EnumerateVariants produces must survive
// a decode round trip and a re-encode from its extracted free dim values, so a match rule's
// encoding cannot drift from the enumeration the bundle was built from (the c8c9c9e6d class of
// bug — a hand-rolled ordinal pointing outside the reachable set).
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>
#include "base/TGFXTest.h"
#include "gpu/shaders/Families.h"
#include "gpu/shaders/Family.h"
#include "gpu/shaders/modules/Xfer.h"

namespace tgfx {
namespace {

/// Appends one slot's free dim values of the variant, in flattened declaration order — the
/// argument list EncodeVariant expects. Pinned dims are skipped; the geometry's dims are always
/// free (pinning is a body-module property).
void AppendFreeDimValues(const Module& module, const int8_t* pinned, uint8_t slot,
                         const Family& family, const Variant& variant,
                         std::vector<uint8_t>* values) {
  const uint8_t base = DimOffsetOf(family, variant, slot);
  for (uint8_t dim = 0; dim < module.dimCount; ++dim) {
    if (pinned == nullptr || pinned[dim] < 0) {
      values->push_back(variant.values[base + dim]);
    }
  }
}

/// The variant's free dim values in flattened order: geometry first, then body modules, Xfer last.
std::vector<uint8_t> FreeDimValuesOf(const Family& family, const Variant& variant) {
  std::vector<uint8_t> values = {};
  AppendFreeDimValues(*family.geometries[variant.geometry], nullptr, 1, family, variant, &values);
  for (uint8_t index = 0; index < family.moduleCount; ++index) {
    AppendFreeDimValues(*family.modules[index].module, family.modules[index].pinned,
                        static_cast<uint8_t>(index + 2), family, variant, &values);
  }
  AppendFreeDimValues(XferModule(), nullptr, static_cast<uint8_t>(family.moduleCount + 2), family,
                      variant, &values);
  return values;
}

/// Family names must be unique, or FamilyIndexOf addresses two rows with one constant.
TGFX_TEST(FamilyTest, FamilyNamesAreUnique) {
  std::vector<std::string> names = {};
  for (size_t index = 0; index < FamilyCount(); ++index) {
    names.push_back(Families()[index].name);
  }
  std::sort(names.begin(), names.end());
  for (size_t index = 1; index < names.size(); ++index) {
    EXPECT_NE(names[index - 1], names[index]);
  }
}

TGFX_TEST(FamilyTest, EveryEnumeratedVariantRoundTripsThroughDecodeAndEncode) {
  for (uint16_t index = 0; index < FamilyCount(); ++index) {
    const Family& family = Families()[index];
    const auto variants = EnumerateVariants(family, index);
    ASSERT_FALSE(variants.empty()) << family.name;
    for (const auto& variant : variants) {
      // Decode rebuilds the full variant (pinned values included) from the ordinal alone.
      auto decoded = DecodeVariant(index, variant.ordinal);
      ASSERT_TRUE(decoded.has_value()) << family.name << " ordinal " << variant.ordinal;
      EXPECT_EQ(decoded->family, variant.family);
      EXPECT_EQ(decoded->geometry, variant.geometry);
      EXPECT_EQ(decoded->values, variant.values);
      EXPECT_EQ(decoded->ordinal, variant.ordinal);
      EXPECT_EQ(decoded->id(), variant.id());
      // Re-encoding the extracted free values reproduces the enumerated variant exactly.
      auto encoded = EncodeVariant(index, variant.geometry, FreeDimValuesOf(family, variant));
      ASSERT_TRUE(encoded.has_value()) << family.name << " ordinal " << variant.ordinal;
      EXPECT_EQ(encoded->values, variant.values);
      EXPECT_EQ(encoded->ordinal, variant.ordinal);
      EXPECT_EQ(encoded->id(), variant.id());
    }
  }
}

TGFX_TEST(FamilyTest, EncodeAndDecodeRejectWhatTheDeclarationRejects) {
  // The lookups stay in constant expressions: a runtime evaluation would odr-use the undefined
  // FamilyNameNotFound even on the found path (see Families.h).
  constexpr uint16_t SolidColorIndex = FamilyIndexOf("SolidColor");
  constexpr uint16_t TiledTextureFillIndex = FamilyIndexOf("TiledTextureFill");
  constexpr uint16_t RRectIndex = FamilyIndexOf("RRect");
  constexpr uint16_t ShapeInstancedIndex = FamilyIndexOf("ShapeInstanced");
  const auto PastTheTable = static_cast<uint16_t>(FamilyCount());
  // A geometry the family does not carry.
  EXPECT_FALSE(EncodeVariant(SolidColorIndex, 1, {0}).has_value());
  // An ordinal past the family's variant space (SolidColor's is HAS_XP's 3 values).
  EXPECT_FALSE(DecodeVariant(SolidColorIndex, 3).has_value());
  EXPECT_FALSE(DecodeVariant(TiledTextureFillIndex, 6).has_value());
  // A family index past the table.
  EXPECT_FALSE(EncodeVariant(PastTheTable, 0, {0}).has_value());
  EXPECT_FALSE(DecodeVariant(PastTheTable, 0).has_value());
  // The RRect family keeps the [PRESENT, HAS_XP] product (6 forms): the stroke form is a runtime
  // uniform now, so every form decodes and only an ordinal past the space is rejected.
  EXPECT_TRUE(DecodeVariant(RRectIndex, 5).has_value());
  EXPECT_FALSE(DecodeVariant(RRectIndex, 6).has_value());
  // The old hand-rolled ShapeInstanced ordinal (HAS_XP alone) landed outside the reachable set
  // for every blended xfer: ordinal 1 is VERTEX_COLOR's filtered-out value, and the dst-texture
  // form lives at 2 * HAS_XP = 2.
  EXPECT_FALSE(DecodeVariant(ShapeInstancedIndex, 1).has_value());
  auto dstForm = DecodeVariant(ShapeInstancedIndex, 2);
  ASSERT_TRUE(dstForm.has_value());
  EXPECT_EQ(dstForm->values[1], 1u);
}

}  // namespace
}  // namespace tgfx

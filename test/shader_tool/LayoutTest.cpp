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

#include <gtest/gtest.h>
#include <set>
#include <string>
#include "TestDeclaredModules.h"
#include "gpu/shaders/Families.h"
#include "gpu/shaders/FamilyDomains.h"
#include "gpu/shaders/Layout.h"

namespace tgfx {
namespace {

// The RRect family's slot layout: Frame(0), RRectGeometry(1), TiledTexture(2), AnalyticClip(3),
// Xfer(4). The flattened dims: [STROKE, FULL, PRESENT, HAS_XP].
uint16_t FamilyIndexByName(const char* name) {
  for (size_t index = 0; index < FamilyCount(); ++index) {
    if (std::string(Families()[index].name) == name) {
      return static_cast<uint16_t>(index);
    }
  }
  return 0xFFFF;
}

Variant RRectVariant(uint8_t stroke, uint8_t present, uint8_t hasXp) {
  Variant variant = {};
  variant.family = FamilyIndexByName("RRect");
  variant.geometry = 0;
  variant.values[0] = stroke;
  variant.values[1] = 0;
  variant.values[2] = present;
  variant.values[3] = hasXp;
  variant.ordinal = static_cast<uint32_t>(stroke) + 2u * present + 4u * hasXp;
  return variant;
}

}  // namespace

TEST(LayoutTest, RRectEnumerationMatchesTheAllowedRule) {
  const uint16_t rrect = FamilyIndexByName("RRect");
  ASSERT_NE(rrect, 0xFFFF);
  const auto variants = EnumerateVariants(Families()[rrect], rrect);
  ASSERT_EQ(variants.size(), 9u);
  std::set<uint32_t> ordinals;
  for (const auto& variant : variants) {
    ordinals.insert(variant.ordinal);
    const uint8_t stroke = DimValueOf(Families()[rrect], variant, 1, 0);
    const uint8_t present = DimValueOf(Families()[rrect], variant, 2, 1);
    EXPECT_FALSE(stroke != 0 && present != 0);
    // The pinned FULL dim never iterates.
    EXPECT_EQ(DimValueOf(Families()[rrect], variant, 2, 0), 0u);
  }
  EXPECT_EQ(ordinals.size(), 9u);
}

TEST(LayoutTest, SolidFillLayout) {
  const auto layout = DeriveLayout(RRectVariant(0, 0, 0));
  // Vertex block: RTAdjust only; fragment block: geometry color pair, the clip contract, no XP.
  EXPECT_EQ(layout.vertexBlockSize, 16u);
  EXPECT_GT(layout.fragmentBlockSize, 0u);
  EXPECT_EQ(layout.attributes.size(), 5u);
  EXPECT_EQ(layout.varyings.size(), 4u);
  EXPECT_TRUE(layout.textures.empty());
  // The frame slice is the first field.
  const auto frame = layout.SliceOf(0);
  ASSERT_EQ(frame.count, 1u);
  EXPECT_EQ(frame.names[0], "RTAdjust");
  EXPECT_EQ(frame.fields[0].block, 0u);
  EXPECT_EQ(frame.fields[0].offset, 0u);
  // The geometry's fragment pair rides after nothing else in its block.
  const auto geometry = layout.SliceOf(1);
  ASSERT_EQ(geometry.count, 2u);
  EXPECT_EQ(geometry.names[0], "RRectGeometry_CommonColor");
  EXPECT_EQ(geometry.names[1], "RRectGeometry_HasCommonColor");
}

TEST(LayoutTest, TexturedLayoutNumbersTexturesAndVaryings) {
  const auto layout = DeriveLayout(RRectVariant(0, 1, 1));
  // Attributes: the five unconditional ones (stroke is off).
  EXPECT_EQ(layout.attributes.size(), 5u);
  // Varyings: the four geometry ones plus TiledTexture's coord.
  ASSERT_EQ(layout.varyings.size(), 5u);
  EXPECT_EQ(layout.varyings[4].name, "TiledTexture_vCoord");
  // Textures: the tiled sampler at 0, the xfer dst at 1.
  ASSERT_EQ(layout.textures.size(), 2u);
  EXPECT_EQ(layout.textures[0].name, "TiledTexture_Sampler");
  EXPECT_EQ(layout.textures[0].binding, 0u);
  EXPECT_EQ(layout.textures[1].name, "DstTextureSampler");
  EXPECT_EQ(layout.textures[1].binding, 1u);
  // The tiled slice carries the coord matrix in the vertex block and the recipe in the fragment
  // (the sampler and the varying are interface entries, not uniform fields).
  const auto tiled = layout.SliceOf(2);
  EXPECT_EQ(tiled.count, 8u);
  EXPECT_EQ(tiled.names[0], "TiledTexture_CoordMatrix");
  EXPECT_EQ(tiled.fields[0].block, 0u);
  // The xfer slice exists (dst dialect) with the three fields.
  const auto xfer = layout.SliceOf(4);
  ASSERT_EQ(xfer.count, 4u);
  EXPECT_EQ(xfer.names[2], "XPBlendMode");
  EXPECT_EQ(xfer.names[3], "OutputAlphaSwizzle");
}

TEST(LayoutTest, StrokeLayoutAddsTheAttributeAndVarying) {
  const auto layout = DeriveLayout(RRectVariant(1, 0, 0));
  ASSERT_EQ(layout.attributes.size(), 6u);
  EXPECT_EQ(layout.attributes[5].name, "RRectGeometry_StrokeWidth");
  EXPECT_EQ(layout.attributes[5].location, 5u);
  ASSERT_EQ(layout.varyings.size(), 5u);
  EXPECT_EQ(layout.varyings[4].name, "RRectGeometry_vStrokeWidth");
}

TEST(LayoutTest, FramebufferFetchKeepsTheUnusedDstCoordFields) {
  // HAS_XP == 2 declares the dst-coord pair too (one `when` rule covers the three fields); the
  // shader never reads them, and the reflection equality check pins the declared set.
  const auto layout = DeriveLayout(RRectVariant(0, 0, 2));
  const auto xfer = layout.SliceOf(4);
  ASSERT_EQ(xfer.count, 4u);
  EXPECT_EQ(xfer.names[0], "DstTextureUpperLeft");
  EXPECT_EQ(xfer.names[1], "DstTextureCoordScale");
  EXPECT_EQ(xfer.names[2], "XPBlendMode");
  EXPECT_EQ(xfer.names[3], "OutputAlphaSwizzle");
  // The fetch dialect reads the destination through the subpass input, so it declares no dst
  // sampler at all: a declared-but-unbound sampler would shift every later texture binding.
  EXPECT_TRUE(layout.textures.empty());
}

TEST(LayoutTest, DomainViewMatchesTheOrdinal) {
  const uint16_t rrect = FamilyIndexByName("RRect");
  ASSERT_NE(rrect, 0xFFFF);
  const auto info = AssembledFamilyInfo(Families()[rrect]);
  EXPECT_EQ(info.name, std::string("RRect"));
  EXPECT_EQ(info.fragDomain.totalCount(), 12u);
  const uint8_t strokeValues[] = {0, 1};
  const uint8_t presentValues[] = {0, 1};
  const int xpValues[] = {XP_NONE, XP_DST_TEXTURE, XP_FRAMEBUFFER_FETCH};
  for (auto stroke : strokeValues) {
    for (auto present : presentValues) {
      for (auto hasXp : xpValues) {
        const auto variant = RRectVariant(stroke, present, static_cast<uint8_t>(hasXp));
        EXPECT_EQ(info.fragDomain.valueOf(variant.ordinal, "RRectGeometry_STROKE"), stroke);
        EXPECT_EQ(info.fragDomain.valueOf(variant.ordinal, "TiledTexture_PRESENT"), present);
        EXPECT_EQ(info.fragDomain.valueOf(variant.ordinal, XP_DEFINE_NAME), hasXp);
      }
    }
  }
}

namespace {

const Layout::Field* FindField(const Layout& layout, const char* name) {
  for (size_t index = 0; index < layout.fields.size(); ++index) {
    if (layout.fieldNames[index] == name) {
      return &layout.fields[index];
    }
  }
  return nullptr;
}

}  // namespace

TEST(LayoutTest, SpecIdsFollowModuleThenElementOrder) {
  const auto layout =
      DeriveLayout(test_modules::TestFamily, test_modules::TestVariant(1));  // CAP = 32
  ASSERT_EQ(layout.specFields.size(), 2u);
  // SlotCount: the scalar spec field, one id, in the body slot (Frame 0, geometry 1, body 2).
  EXPECT_EQ(layout.specFields[0].firstId, 1u);
  EXPECT_EQ(layout.specFields[0].slot, 2u);
  // SlotPacked follows with four ids per element (32 elements => ids 2..129).
  EXPECT_EQ(layout.specFields[1].firstId, 2u);
}

TEST(LayoutTest, PoolLengthsFollowTheCapacityDim) {
  const uint16_t expected[] = {16, 32, 3};
  const uint16_t expectedSize[] = {16 * 16, 32 * 16, 3 * 16};
  for (uint8_t cap = 0; cap < 3; ++cap) {
    const auto layout = DeriveLayout(test_modules::TestFamily, test_modules::TestVariant(cap));
    // The spec Int4 pool and the plain Float4 payload both read their length from the CAP dim.
    const auto* slotPacked = FindField(layout, "TestSpec_SlotPacked");
    const auto* payload = FindField(layout, "TestSpec_Payload");
    ASSERT_NE(slotPacked, nullptr);
    ASSERT_NE(payload, nullptr);
    EXPECT_EQ(slotPacked->arraySize, expected[cap]) << static_cast<int>(cap);
    EXPECT_EQ(payload->arraySize, expected[cap]) << static_cast<int>(cap);
    // The std140 footprint: one 16-byte stride per array element.
    EXPECT_EQ(payload->size, expectedSize[cap]) << static_cast<int>(cap);
  }
}

TEST(LayoutTest, WhenMaskSelectsExactlyTheMaskedValues) {
  const Element& gated = test_modules::TestSpecElements[2];
  // The gated element's mask is 0x02: it exists only at the dim value 1.
  EXPECT_FALSE(ElementPresentInVariant(gated, 0));
  EXPECT_TRUE(ElementPresentInVariant(gated, 1));
  // The old "not zero" test over a dim of n values is every bit but the first.
  EXPECT_EQ(WhenNotZero(2), 0x02u);
  EXPECT_EQ(WhenNotZero(3), 0x06u);
  EXPECT_EQ(WhenNotZero(5), 0x1Eu);
}

TEST(LayoutTest, SpecElementsLeaveTheUniformFieldIntact) {
  const auto layout = DeriveLayout(test_modules::TestFamily, test_modules::TestVariant(0));
  // A spec element is a uniform field like any other: the generic compile always reads it, and
  // the bundle's reflection covers it. The field names carry the module prefix.
  const auto body = layout.SliceOf(2);
  ASSERT_GE(body.count, 3u);
  EXPECT_EQ(body.names[0], "TestSpec_SlotCount");
  EXPECT_EQ(body.names[1], "TestSpec_SlotPacked");
  EXPECT_EQ(body.fields[1].arraySize, 16u);
}

}  // namespace tgfx

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
#include "gpu/shaders/FamilyDomains.h"
#include "gpu/shaders/Families.h"
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
  // The dst sampler stays declared under the fetch dialect too (the shader never reads it; the
  // runtime binds the shared dummy texture), so one `when` rule covers the three fields.
  ASSERT_EQ(layout.textures.size(), 1u);
  EXPECT_EQ(layout.textures[0].name, "DstTextureSampler");
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

}  // namespace tgfx

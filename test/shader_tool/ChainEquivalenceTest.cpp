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
//  either express or implied. See the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

// The declared Chain family must serve exactly the legacy interpreter's reachable set: one new
// variant per legacy (vert, frag) permutation, and nothing else. The test pairs both sides by
// their dimension values (the legacy permutation space and the declared variant space describe
// the same cells), so a mask, an allowed rule or a geometry split that drifts from the legacy
// matcher fails here instead of at pixel comparison.

#include <gtest/gtest.h>
#include <set>
#include <string>
#include <vector>
#include "gpu/shaders/ChainInputs.h"
#include "gpu/shaders/Families.h"
#include "gpu/shaders/Layout.h"
#include "gpu/shaders/PrecompiledShader.h"

namespace tgfx {
namespace {

uint16_t ChainFamilyIndex() {
  for (size_t index = 0; index < FamilyCount(); ++index) {
    if (std::string(Families()[index].name) == "Chain") {
      return static_cast<uint16_t>(index);
    }
  }
  return 0xFFFF;
}

std::string CellKey(int gpLayout, int xp, int coverage, int uvCoord, int textureCount, int mask,
                    int subset, int resource) {
  return std::to_string(gpLayout) + "/" + std::to_string(xp) + "/" + std::to_string(coverage) +
         "/" + std::to_string(uvCoord) + "/" + std::to_string(textureCount) + "/" +
         std::to_string(mask) + "/" + std::to_string(subset) + "/" + std::to_string(resource);
}

/// The profile's (texture count, mask, resource kind) cell, in the legacy dimension terms.
void ProfileCell(int profile, int* textureCount, int* mask, int* resource) {
  *textureCount = 0;
  *mask = 0;
  *resource = 0;
  switch (profile) {
    case 1:
      *mask = 1;
      break;
    case 2:
      *textureCount = 1;
      break;
    case 3:
      *resource = 1;
      break;
    case 4:
      *mask = 1;
      *resource = 1;
      break;
    case 5:
      *textureCount = 1;
      *resource = 1;
      break;
    case 6:
      *textureCount = 1;
      *resource = 2;
      break;
    default:
      break;
  }
}

}  // namespace

TEST(ChainEquivalenceTest, DeclaredVariantsBijectTheLegacyReachableSet) {
  const auto chainIndex = ChainFamilyIndex();
  ASSERT_NE(chainIndex, 0xFFFF);
  // The legacy family retired; its composition rules remain in ChainInputs, which enumerates the
  // frozen reference set in the legacy (vert, frag) encoding.
  const auto reachable = ChainInputs::Reachable();
  const auto vertDomain = ChainInputs::VertDims::domain();
  const auto fragDomain = ChainInputs::FragDims::domain();

  std::set<std::string> legacy;
  for (const auto& pair : reachable) {
    legacy.insert(CellKey(fragDomain.valueOf(pair.second, "GP_LAYOUT"),
                          fragDomain.valueOf(pair.second, "HAS_XP"),
                          fragDomain.valueOf(pair.second, "HAS_COVERAGE"),
                          vertDomain.valueOf(pair.first, "HAS_UV_COORD"),
                          fragDomain.valueOf(pair.second, "TEXTURE_COUNT"),
                          fragDomain.valueOf(pair.second, "HAS_MASK_TEXTURE"),
                          fragDomain.valueOf(pair.second, "HAS_SUBSET"),
                          fragDomain.valueOf(pair.second, "RESOURCE_KIND")));
  }
  ASSERT_EQ(legacy.size(), 147u);

  const Family& family = Families()[chainIndex];
  const auto variants = EnumerateVariants(family, chainIndex);
  std::set<std::string> declared;
  for (const auto& variant : variants) {
    const uint8_t subset = DimValueOf(family, variant, 1, 0);
    const uint8_t profile = DimValueOf(family, variant, 2, 0);
    const uint8_t xp = DimValueOf(family, variant, 3, 0);
    int gpLayout = 0;
    int coverage = 1;
    int uvCoord = 0;
    switch (variant.geometry) {
      case 0:  // ChainRectGeometry
        break;
      case 1:  // ChainRectUVGeometry
        uvCoord = 1;
        break;
      case 2:  // ChainAtlasGeometry
        coverage = 0;
        uvCoord = 1;
        break;
      case 3:  // ChainEllipseGeometry
        gpLayout = 1;
        coverage = 0;
        break;
      case 4:  // ChainHairlineGeometry
        gpLayout = 2;
        coverage = 0;
        break;
      case 5:  // ChainComplexGeometry
        gpLayout = 3;
        coverage = 0;
        break;
      case 6:  // ChainRRectGeometry
        gpLayout = 5;
        coverage = 0;
        break;
      case 7:  // ChainComplexRRectGeometry
        gpLayout = 7;
        coverage = 0;
        break;
      default:  // ChainHairlineQuadGeometry
        gpLayout = 9;
        coverage = 0;
        break;
    }
    int textureCount = 0;
    int mask = 0;
    int resource = 0;
    ProfileCell(profile, &textureCount, &mask, &resource);
    declared.insert(CellKey(gpLayout, xp, coverage, uvCoord, textureCount, mask, subset, resource));
  }
  EXPECT_EQ(declared.size(), variants.size());
  EXPECT_EQ(declared, legacy);
}

TEST(ChainEquivalenceTest, SpecFieldsKeepTheLegacyIdOrder) {
  const auto chainIndex = ChainFamilyIndex();
  ASSERT_NE(chainIndex, 0xFFFF);
  const Family& family = Families()[chainIndex];
  const auto variants = EnumerateVariants(family, chainIndex);
  ASSERT_FALSE(variants.empty());
  for (const auto& variant : variants) {
    const auto fields = DeriveSpecFields(family, variant);
    ASSERT_EQ(fields.size(), 5u);
    const uint8_t profile = DimValueOf(family, variant, 2, 0);
    const uint16_t capacity = profile == 6 ? 32 : 16;
    // The legacy first-use order: SlotCount, SlotPacked, CoverageRootIndex, RootIndex,
    // ClipCoverageRegister; the array takes four ids per element.
    EXPECT_EQ(fields[0].firstId, 1u);
    EXPECT_EQ(fields[1].firstId, 2u);
    EXPECT_EQ(fields[2].firstId, static_cast<uint16_t>(2 + 4 * capacity));
    EXPECT_EQ(fields[3].firstId, static_cast<uint16_t>(3 + 4 * capacity));
    EXPECT_EQ(fields[4].firstId, static_cast<uint16_t>(4 + 4 * capacity));
    const auto modules = ModulesOf(family, variant);
    const std::vector<std::string> expected = {"SlotCount", "SlotPacked", "CoverageRootIndex",
                                               "RootIndex", "ClipCoverageRegister"};
    for (size_t index = 0; index < fields.size(); ++index) {
      const Module* module = modules.list[fields[index].slot];
      ASSERT_NE(module, nullptr);
      EXPECT_EQ(std::string(module->elements[fields[index].element].name), expected[index]);
    }
  }
}

}  // namespace tgfx

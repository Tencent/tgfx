//////////////////////////////////////////////////////////////////////////////////////////////////
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
//////////////////////////////////////////////////////////////////////////////////////////////////

// Which variants the tool compiles is decided in the declaration layer: every family names its
// dimensions and the rule that maps a draw to their values, and the reachable set is that rule
// applied to the family's inputs. These tests state the properties the whole bundle depends on,
// for every registered family, so a new family is covered without writing another test.

#include <set>
#include <string>
#include <vector>
#include "gpu/ShaderProfile.h"
#include "gpu/shaders/AllShaderFamilies.h"
#include "gpu/shaders/PrecompiledShader.h"
#include "gpu/shaders/ShaderPermutation.h"
#include "gpu/shaders/level1/QuadTextureFillShader.h"
#include "gtest/gtest.h"

namespace tgfx {
namespace {

std::vector<PrecompiledShaderInfo> AllFamilies() {
  std::vector<PrecompiledShaderInfo> families;
  for (const auto& factory : ShaderRegistry::All()) {
    families.push_back(factory()->info());
  }
  return families;
}

// The define name of any kind of dimension, for std::visit.
struct DefineNameOf {
  template <typename Dimension>
  std::string operator()(const Dimension& dimension) const {
    return dimension.defineName;
  }
};

}  // namespace

TEST(FamilyRegistryTest, EveryFamilyIsRegisteredOnceWithItsOwnFiles) {
  auto families = AllFamilies();
  EXPECT_EQ(families.size(), 23u);
  std::set<std::string> names;
  for (const auto& info : families) {
    EXPECT_TRUE(names.insert(info.name).second) << "duplicate family " << info.name;
    EXPECT_FALSE(info.vertexFile.empty()) << info.name;
    EXPECT_FALSE(info.fragmentFile.empty()) << info.name;
    EXPECT_EQ(info.vertexFile.rfind("level1/", 0), 0u) << info.name;
    EXPECT_EQ(info.fragmentFile.rfind("level1/", 0), 0u) << info.name;
  }
}

TEST(FamilyRegistryTest, FindAndEnumerateAgreeWithTheRegisteredFamilies) {
  for (const auto& info : AllFamilies()) {
    auto found = ShaderRegistry::Find(info.name);
    ASSERT_NE(found, nullptr) << info.name;
    EXPECT_EQ(found->name, info.name);
    EXPECT_TRUE(ShaderRegistry::Enumerate(info.name).has_value()) << info.name;
    EXPECT_TRUE(EnumerateReachablePermutations(info.name).has_value()) << info.name;
  }
  EXPECT_EQ(ShaderRegistry::Find("NoSuchFamily"), nullptr);
  EXPECT_FALSE(ShaderRegistry::Enumerate("NoSuchFamily").has_value());
  EXPECT_FALSE(EnumerateReachablePermutations("NoSuchFamily").has_value());
}

TEST(ReachabilityTest, EveryFamilyHasAtLeastOneReachableVariant) {
  for (const auto& info : AllFamilies()) {
    auto reachable = EnumerateReachablePermutations(info.name);
    ASSERT_TRUE(reachable.has_value()) << info.name;
    EXPECT_FALSE(reachable->empty()) << info.name << " would compile nothing";
  }
}

TEST(ReachabilityTest, EveryReachableVariantIsBuildable) {
  // Buildable: both indices lie inside their domains and the mirrored dimensions the two stages
  // share (the varying interface) agree. The tool builds exactly these, so one that is not
  // buildable would be a variant the compiler cannot produce a consistent program for.
  for (const auto& info : AllFamilies()) {
    auto reachable = EnumerateReachablePermutations(info.name);
    ASSERT_TRUE(reachable.has_value()) << info.name;
    for (const auto& pair : *reachable) {
      EXPECT_TRUE(IsBuildablePermutation(info, pair.first, pair.second))
          << info.name << " (" << pair.first << ", " << pair.second << ")";
    }
  }
}

TEST(ReachabilityTest, TheReachableSetIsSmallerThanTheFullProductWherePossible) {
  // Reachable is what the rules can produce, not the Cartesian product of the domains: the
  // product is what the old domain walk compiled. At least the large families must be sparse, or
  // the bundle would carry variants nothing can request.
  size_t sparse = 0;
  for (const auto& info : AllFamilies()) {
    auto reachable = EnumerateReachablePermutations(info.name);
    ASSERT_TRUE(reachable.has_value());
    uint64_t product = static_cast<uint64_t>(info.vertDomain.totalCount()) *
                       static_cast<uint64_t>(info.fragDomain.totalCount());
    EXPECT_LE(reachable->size(), product) << info.name;
    if (reachable->size() < product) {
      ++sparse;
    }
  }
  EXPECT_GT(sparse, 0u);
}

TEST(ReachabilityTest, ADomainIndexOutsideTheDomainIsNotBuildable) {
  for (const auto& info : AllFamilies()) {
    EXPECT_FALSE(IsBuildablePermutation(info, info.vertDomain.totalCount(), 0)) << info.name;
    EXPECT_FALSE(IsBuildablePermutation(info, 0, info.fragDomain.totalCount())) << info.name;
  }
}

TEST(ReachabilityTest, EnumerationIsDeterministic) {
  for (const auto& info : AllFamilies()) {
    auto first = EnumerateReachablePermutations(info.name);
    auto second = EnumerateReachablePermutations(info.name);
    ASSERT_TRUE(first.has_value() && second.has_value());
    EXPECT_EQ(*first, *second) << info.name;
  }
}

TEST(ReachabilityTest, EveryDimensionDefineNameIsAnIdentifier) {
  // The names are written into #define lines, so one with a space or a symbol would corrupt the
  // text the compiler receives.
  for (const auto& info : AllFamilies()) {
    for (const auto* domain : {&info.vertDomain, &info.fragDomain}) {
      for (const auto& dimension : domain->getDimensions()) {
        std::string name = std::visit(DefineNameOf(), dimension);
        ASSERT_FALSE(name.empty()) << info.name;
        EXPECT_TRUE(std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_')
            << info.name << " " << name;
        for (char ch : name) {
          EXPECT_TRUE(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_')
              << info.name << " " << name;
        }
      }
    }
  }
}

TEST(ReachabilityTest, FramebufferFetchVariantsExistOnlyWhereTheProfileCanServeThem) {
  // The profile rule removes exactly the variants whose destination read is a framebuffer fetch.
  for (const auto& info : AllFamilies()) {
    auto reachable = EnumerateReachablePermutations(info.name);
    ASSERT_TRUE(reachable.has_value());
    size_t fetch = 0;
    for (const auto& pair : *reachable) {
      if (info.fragDomain.valueOf(pair.second, XP_DEFINE_NAME) == XP_FRAMEBUFFER_FETCH) {
        ++fetch;
      }
    }
    for (auto profile : AllShaderProfiles) {
      size_t compiled = 0;
      for (const auto& pair : *reachable) {
        if (PermutationCompilesForBackend(info, pair.first, pair.second, profile)) {
          ++compiled;
        }
      }
      size_t expected = ProfileSupportsFramebufferFetch(profile) ? reachable->size()
                                                                 : reachable->size() - fetch;
      EXPECT_EQ(compiled, expected) << info.name << " on " << ProfileTag(profile);
    }
  }
}

// ---- Mirrored dimensions --------------------------------------------------------------------

TEST(MirroredDimensionsTest, StagesSharingADimensionMustAgreeOnItsValue) {
  // HAS_UV is declared by both stages: the vertex stage writes the varying the fragment stage
  // reads, so the two must be built with the same value. HAS_SUBSET belongs to the vertex stage
  // only and may differ freely.
  PermutationDomain vertex({PermutationBool("HAS_UV"), PermutationBool("HAS_SUBSET")});
  PermutationDomain fragment({PermutationBool("HAS_UV"), PermutationInt("HAS_XP", 3)});
  EXPECT_TRUE(MirroredDimsAgree(vertex, fragment, {1, 0}, {1, 2}));
  EXPECT_TRUE(MirroredDimsAgree(vertex, fragment, {0, 1}, {0, 0}));
  EXPECT_FALSE(MirroredDimsAgree(vertex, fragment, {1, 0}, {0, 2}));
  EXPECT_FALSE(MirroredDimsAgree(vertex, fragment, {0, 1}, {1, 1}));
}

TEST(MirroredDimensionsTest, DomainsWithoutACommonDimensionAlwaysAgree) {
  PermutationDomain vertex({PermutationBool("A")});
  PermutationDomain fragment({PermutationBool("B")});
  EXPECT_TRUE(MirroredDimsAgree(vertex, fragment, {1}, {0}));
  EXPECT_TRUE(MirroredDimsAgree(vertex, fragment, {0}, {1}));
}

TEST(MirroredDimensionsTest, TheRealFamiliesDisagreeSomewhereInTheFullProduct) {
  // The full product of the two domains contains pairs whose mirrored dimensions differ, which is
  // why a variant has to be checked before it is built. A family with no such pair would make the
  // check vacuous for it, so at least one family must exercise the rejection.
  size_t rejected = 0;
  for (const auto& info : AllFamilies()) {
    for (uint32_t vert = 0; vert < info.vertDomain.totalCount(); ++vert) {
      for (uint32_t frag = 0; frag < info.fragDomain.totalCount(); ++frag) {
        if (!IsBuildablePermutation(info, vert, frag)) {
          ++rejected;
        }
      }
    }
  }
  EXPECT_GT(rejected, 0u);
}

// ---- PermutationDomain ----------------------------------------------------------------------

TEST(PermutationDomainTest, EncodeAndDecodeAreInverseForEveryIndex) {
  PermutationDomain domain({PermutationBool("A"), PermutationEnum("B", {"x", "y", "z"}),
                            PermutationInt("C", 4), PermutationBool("D")});
  ASSERT_EQ(domain.totalCount(), 2u * 3u * 4u * 2u);
  for (uint32_t index = 0; index < domain.totalCount(); ++index) {
    EXPECT_EQ(domain.encode(domain.decode(index)), index) << index;
  }
}

TEST(PermutationDomainTest, TheFirstDimensionVariesFastest) {
  // Mixed radix, least significant digit first: the order is part of every variant index in every
  // published bundle.
  PermutationDomain domain({PermutationBool("A"), PermutationInt("B", 3)});
  EXPECT_EQ(domain.decode(0), (std::vector<int>{0, 0}));
  EXPECT_EQ(domain.decode(1), (std::vector<int>{1, 0}));
  EXPECT_EQ(domain.decode(2), (std::vector<int>{0, 1}));
  EXPECT_EQ(domain.decode(5), (std::vector<int>{1, 2}));
}

TEST(PermutationDomainTest, DefineListsCarryEveryDimensionEvenAtZero) {
  PermutationDomain domain({PermutationBool("A"), PermutationEnum("B", {"x", "y"}),
                            PermutationInt("C", 5)});
  auto defines = domain.defineListFor(domain.encode({0, 1, 4}));
  ASSERT_EQ(defines.size(), 3u);
  EXPECT_EQ(defines[0], "A=0");
  EXPECT_EQ(defines[1], "B=1");
  EXPECT_EQ(defines[2], "C=4");
}

TEST(PermutationDomainTest, ValueOfFindsADimensionByItsDefineName) {
  PermutationDomain domain({PermutationBool("A"), PermutationInt("C", 5)});
  auto index = domain.encode({1, 3});
  EXPECT_EQ(domain.valueOf(index, "A"), 1);
  EXPECT_EQ(domain.valueOf(index, "C"), 3);
  EXPECT_EQ(domain.valueOf(index, "Missing"), 0);
  EXPECT_TRUE(domain.hasDimension("A"));
  EXPECT_FALSE(domain.hasDimension("Missing"));
}

TEST(PermutationDomainTest, BoolNamesBuildABooleanDomain) {
  auto domain = PermutationDomain::FromBoolNames("HAS_A, HAS_B,HAS_C");
  EXPECT_EQ(domain.dimensionCount(), 3u);
  EXPECT_EQ(domain.totalCount(), 8u);
  EXPECT_TRUE(domain.hasDimension("HAS_A"));
  EXPECT_TRUE(domain.hasDimension("HAS_B"));
  EXPECT_TRUE(domain.hasDimension("HAS_C"));
}

TEST(PermutationDomainTest, ARealFamilyKeepsItsPublishedIndexLayout) {
  // The variant indices are stored in every published bundle and hashed into every stage key, so a
  // family's index layout cannot change without invalidating them. QuadTextureFillShader's
  // fragment dimensions are HAS_SUBSET (2 values), HAS_XP (3) and HAS_LOCAL_MASK (2); the first
  // varies fastest. The indices below were worked out by hand, not read back from the
  // implementation.
  auto info = QuadTextureFillShader().info();
  const auto& domain = info.fragDomain;
  ASSERT_EQ(domain.totalCount(), 12u);
  // index = subset + 2 * xp + 6 * localMask
  EXPECT_EQ(domain.encode({0, 0, 0}), 0u);
  EXPECT_EQ(domain.encode({1, 0, 0}), 1u);
  EXPECT_EQ(domain.encode({0, 1, 0}), 2u);
  EXPECT_EQ(domain.encode({1, 2, 0}), 5u);
  EXPECT_EQ(domain.encode({0, 0, 1}), 6u);
  EXPECT_EQ(domain.encode({1, 2, 1}), 11u);
  auto defines = domain.defineListFor(9);  // 9 = 1 + 2 * 1 + 6 * 1
  ASSERT_EQ(defines.size(), 3u);
  EXPECT_EQ(defines[0], "HAS_SUBSET=1");
  EXPECT_EQ(defines[1], std::string(XP_DEFINE_NAME) + "=1");
  EXPECT_EQ(defines[2], "HAS_LOCAL_MASK=1");
}

TEST(PermutationDomainTest, AnEmptyDomainHasExactlyOneVariant) {
  PermutationDomain domain({});
  EXPECT_EQ(domain.totalCount(), 1u);
  EXPECT_TRUE(domain.decode(0).empty());
  EXPECT_EQ(domain.encode({}), 0u);
  EXPECT_TRUE(domain.defineListFor(0).empty());
}

}  // namespace tgfx

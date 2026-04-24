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

#include <unordered_set>
#include "gpu/ShaderMacroSet.h"
#include "gpu/processors/AtlasTextGeometryProcessor.h"
#include "gpu/processors/ColorSpaceXFormEffect.h"
#include "gpu/processors/ConicGradientLayout.h"
#include "gpu/processors/DefaultGeometryProcessor.h"
#include "gpu/processors/DeviceSpaceTextureEffect.h"
#include "gpu/processors/DiamondGradientLayout.h"
#include "gpu/processors/EllipseGeometryProcessor.h"
#include "gpu/processors/GaussianBlur1DFragmentProcessor.h"
#include "gpu/processors/HairlineLineGeometryProcessor.h"
#include "gpu/processors/HairlineQuadGeometryProcessor.h"
#include "gpu/processors/LinearGradientLayout.h"
#include "gpu/processors/MeshGeometryProcessor.h"
#include "gpu/processors/NonAARRectGeometryProcessor.h"
#include "gpu/processors/PorterDuffXferProcessor.h"
#include "gpu/processors/QuadPerEdgeAAGeometryProcessor.h"
#include "gpu/processors/RadialGradientLayout.h"
#include "gpu/processors/RoundStrokeRectGeometryProcessor.h"
#include "gpu/processors/ShapeInstancedGeometryProcessor.h"
#include "gpu/processors/UnrolledBinaryGradientColorizer.h"
#include "gpu/variants/ShaderVariant.h"
#include "gtest/gtest.h"
#include "tgfx/core/BlendMode.h"

namespace tgfx {

// Helper used by every GP test to assert the shared structural invariants of any
// EnumerateVariants() output: right count, stable indices, non-empty preamble, unique hashes.
static void CheckVariantListInvariants(const std::vector<ShaderVariant>& variants,
                                       size_t expectedCount, const std::string& namePrefix) {
  ASSERT_EQ(variants.size(), expectedCount);
  std::unordered_set<uint64_t> seenHashes;
  seenHashes.reserve(variants.size());
  for (size_t i = 0; i < variants.size(); ++i) {
    const auto& variant = variants[i];
    EXPECT_EQ(variant.index, static_cast<int>(i));
    EXPECT_FALSE(variant.name.empty()) << namePrefix << " index=" << i;
    EXPECT_NE(variant.name.find(namePrefix), std::string::npos) << "name=" << variant.name;
    // Preamble may be empty for all-zero configurations (e.g. DefaultGP with coverageAA=0).
    auto inserted = seenHashes.insert(variant.runtimeKeyHash).second;
    EXPECT_TRUE(inserted) << namePrefix << " duplicate hash at index=" << i
                          << " name=" << variant.name;
  }
}

// Matches the kBlendModeCount constant in PorterDuffXferProcessor.cpp. Duplicated here so the
// test catches any drift between the enum and the enumerator.
static constexpr int kExpectedBlendModeCount = static_cast<int>(BlendMode::PlusDarker) + 1;
static constexpr int kExpectedVariantCount = kExpectedBlendModeCount * 2;

// 1) Variant count matches (BlendMode count) x (hasDstTexture in {false, true}).
TEST(ShaderVariantTest, PorterDuffXP_Count) {
  auto variants = PorterDuffXferProcessor::EnumerateVariants();
  EXPECT_EQ(variants.size(), static_cast<size_t>(kExpectedVariantCount));
  EXPECT_EQ(kExpectedBlendModeCount, 30)
      << "BlendMode count changed — update kBlendModeCount in PorterDuffXferProcessor.cpp and "
         "this test.";
}

// 2) Each variant's preamble matches what BuildMacros produces for the same configuration.
//    This is the "single source of truth" invariant: runtime onBuildShaderMacros() and offline
//    EnumerateVariants() must agree, since both delegate to BuildMacros.
TEST(ShaderVariantTest, PorterDuffXP_PreambleEquivalence) {
  auto variants = PorterDuffXferProcessor::EnumerateVariants();
  int variantIdx = 0;
  for (int modeInt = 0; modeInt < kExpectedBlendModeCount; ++modeInt) {
    for (int hasDstInt = 0; hasDstInt < 2; ++hasDstInt) {
      auto mode = static_cast<BlendMode>(modeInt);
      bool hasDstTexture = (hasDstInt != 0);
      ShaderMacroSet macros;
      PorterDuffXferProcessor::BuildMacros(mode, hasDstTexture, macros);
      auto expectedPreamble = macros.toPreamble();
      ASSERT_LT(static_cast<size_t>(variantIdx), variants.size());
      EXPECT_EQ(variants[static_cast<size_t>(variantIdx)].preamble, expectedPreamble)
          << "Preamble mismatch at variant index " << variantIdx << " (mode=" << modeInt
          << ", hasDst=" << hasDstInt << ")";
      ++variantIdx;
    }
  }
}

// 3) All variants have unique preamble hashes — different configurations must produce different
//    shader source.
TEST(ShaderVariantTest, PorterDuffXP_HashUniqueness) {
  auto variants = PorterDuffXferProcessor::EnumerateVariants();
  std::unordered_set<uint64_t> seenHashes;
  seenHashes.reserve(variants.size());
  for (const auto& variant : variants) {
    auto inserted = seenHashes.insert(variant.runtimeKeyHash).second;
    EXPECT_TRUE(inserted) << "Duplicate runtimeKeyHash for variant " << variant.name
                          << " (index=" << variant.index << ")";
  }
  EXPECT_EQ(seenHashes.size(), variants.size());
}

// 4) Every variant has a non-empty preamble, non-empty name, and the expected index.
TEST(ShaderVariantTest, PorterDuffXP_StructuralInvariants) {
  auto variants = PorterDuffXferProcessor::EnumerateVariants();
  for (size_t i = 0; i < variants.size(); ++i) {
    const auto& variant = variants[i];
    EXPECT_EQ(variant.index, static_cast<int>(i));
    EXPECT_FALSE(variant.name.empty()) << "index=" << i;
    EXPECT_FALSE(variant.preamble.empty()) << "index=" << i;
    // Every PorterDuffXP variant always defines TGFX_BLEND_MODE, so the preamble cannot be empty.
    EXPECT_NE(variant.preamble.find("TGFX_BLEND_MODE"), std::string::npos)
        << "index=" << i << " preamble missing TGFX_BLEND_MODE: " << variant.preamble;
  }
}

// 5) Non-coeff blend modes (Overlay..PlusDarker, i.e. index > Screen) must have the
//    TGFX_PDXP_NON_COEFF macro defined, regardless of hasDstTexture. Coeff modes must not.
TEST(ShaderVariantTest, PorterDuffXP_NonCoeffMacroCorrectness) {
  auto variants = PorterDuffXferProcessor::EnumerateVariants();
  const auto nonCoeffMarker = std::string("#define TGFX_PDXP_NON_COEFF");
  for (const auto& variant : variants) {
    // Index layout: variant.index = modeInt * 2 + hasDstInt, per BuildMacros iteration order.
    int modeInt = variant.index / 2;
    bool isNonCoeff = modeInt > static_cast<int>(BlendMode::Screen);
    bool preambleHasMarker = variant.preamble.find(nonCoeffMarker) != std::string::npos;
    EXPECT_EQ(preambleHasMarker, isNonCoeff) << "mode=" << modeInt << " (isNonCoeff=" << isNonCoeff
                                             << "), preamble=\"" << variant.preamble << "\"";
  }
}

// 6) hasDstTexture == true ⇔ TGFX_PDXP_DST_TEXTURE_READ present.
TEST(ShaderVariantTest, PorterDuffXP_DstTextureMacroCorrectness) {
  auto variants = PorterDuffXferProcessor::EnumerateVariants();
  const auto dstMarker = std::string("#define TGFX_PDXP_DST_TEXTURE_READ");
  for (const auto& variant : variants) {
    bool hasDstTexture = (variant.index % 2) != 0;  // hasDstInt is the low bit of the index.
    bool preambleHasMarker = variant.preamble.find(dstMarker) != std::string::npos;
    EXPECT_EQ(preambleHasMarker, hasDstTexture)
        << "index=" << variant.index << " preamble=\"" << variant.preamble << "\"";
  }
}

// 7) Spot-check representative variants' preamble contents so regressions in BuildMacros (or in
//    ShaderMacroSet::toPreamble's deterministic ordering) are caught with a clear diff instead
//    of only failing the equivalence loop above.
TEST(ShaderVariantTest, PorterDuffXP_PreambleSpotCheck) {
  auto variants = PorterDuffXferProcessor::EnumerateVariants();
  // Index 6 = SrcOver (mode=3) with hasDst=0 — the hot-path variant for most draws.
  // BuildMacros only emits TGFX_BLEND_MODE for coeff modes without dst. Preamble ordering is
  // alphabetical by macro name, from std::map.
  const auto& srcOverNoDst = variants[static_cast<size_t>(BlendMode::SrcOver) * 2 + 0];
  EXPECT_EQ(srcOverNoDst.name, "PorterDuffXP[mode=SrcOver,dst=0]");
  EXPECT_EQ(srcOverNoDst.preamble, "#define TGFX_BLEND_MODE 3\n");

  // Overlay with dst texture — non-coeff + dst + mode, all three macros present.
  const auto& overlayWithDst = variants[static_cast<size_t>(BlendMode::Overlay) * 2 + 1];
  EXPECT_EQ(overlayWithDst.name, "PorterDuffXP[mode=Overlay,dst=1]");
  EXPECT_EQ(overlayWithDst.preamble,
            "#define TGFX_BLEND_MODE 15\n"
            "#define TGFX_PDXP_DST_TEXTURE_READ 1\n"
            "#define TGFX_PDXP_NON_COEFF 1\n");
}

// ---- Geometry Processor variants ----

TEST(ShaderVariantTest, DefaultGP) {
  auto variants = DefaultGeometryProcessor::EnumerateVariants();
  CheckVariantListInvariants(variants, 2, "DefaultGP");
  // coverageAA=0 -> empty preamble; coverageAA=1 -> TGFX_GP_DEFAULT_COVERAGE_AA.
  EXPECT_EQ(variants[0].preamble, "");
  EXPECT_EQ(variants[1].preamble, "#define TGFX_GP_DEFAULT_COVERAGE_AA 1\n");
}

TEST(ShaderVariantTest, EllipseGP) {
  auto variants = EllipseGeometryProcessor::EnumerateVariants();
  CheckVariantListInvariants(variants, 4, "EllipseGP");
  // Index 3 = stroke=1, commonColor=1 — both macros present, alphabetical order.
  EXPECT_EQ(variants[3].preamble,
            "#define TGFX_GP_ELLIPSE_COMMON_COLOR 1\n"
            "#define TGFX_GP_ELLIPSE_STROKE 1\n");
}

TEST(ShaderVariantTest, HairlineLineGP) {
  auto variants = HairlineLineGeometryProcessor::EnumerateVariants();
  CheckVariantListInvariants(variants, 2, "HairlineLineGP");
  EXPECT_EQ(variants[1].preamble, "#define TGFX_GP_HLINE_COVERAGE_AA 1\n");
}

TEST(ShaderVariantTest, HairlineQuadGP) {
  auto variants = HairlineQuadGeometryProcessor::EnumerateVariants();
  CheckVariantListInvariants(variants, 2, "HairlineQuadGP");
  EXPECT_EQ(variants[1].preamble, "#define TGFX_GP_HQUAD_COVERAGE_AA 1\n");
}

TEST(ShaderVariantTest, MeshGP) {
  auto variants = MeshGeometryProcessor::EnumerateVariants();
  CheckVariantListInvariants(variants, 8, "MeshGP");
  // Index 7 = texCoords=1, colors=1, coverage=1 — all three macros, alphabetical order.
  EXPECT_EQ(variants[7].preamble,
            "#define TGFX_GP_MESH_TEX_COORDS 1\n"
            "#define TGFX_GP_MESH_VERTEX_COLORS 1\n"
            "#define TGFX_GP_MESH_VERTEX_COVERAGE 1\n");
}

TEST(ShaderVariantTest, NonAARRectGP) {
  auto variants = NonAARRectGeometryProcessor::EnumerateVariants();
  CheckVariantListInvariants(variants, 4, "NonAARRectGP");
  // Index 3 = commonColor=1, stroke=1.
  EXPECT_EQ(variants[3].preamble,
            "#define TGFX_GP_NONAA_COMMON_COLOR 1\n"
            "#define TGFX_GP_NONAA_STROKE 1\n");
}

TEST(ShaderVariantTest, QuadPerEdgeAAGP) {
  auto variants = QuadPerEdgeAAGeometryProcessor::EnumerateVariants();
  CheckVariantListInvariants(variants, 16, "QuadPerEdgeAAGP");

  // The SUBSET_MATRIX macro is derived: present iff both SUBSET and UV_MATRIX bits are set.
  // Spot-check two representative variants.
  // Iteration order from EnumerateVariants: aa-cc-uv-subset (outer to inner).
  // Variant aa=0, cc=0, uv=1, subset=1 -> index = 0*8 + 0*4 + 1*2 + 1 = 3.
  EXPECT_EQ(variants[3].preamble,
            "#define TGFX_GP_QUAD_SUBSET 1\n"
            "#define TGFX_GP_QUAD_SUBSET_MATRIX 1\n"
            "#define TGFX_GP_QUAD_UV_MATRIX 1\n");
  // Variant aa=0, cc=0, uv=0, subset=1 -> index = 1. SUBSET but no UV_MATRIX, so no SUBSET_MATRIX.
  EXPECT_EQ(variants[1].preamble, "#define TGFX_GP_QUAD_SUBSET 1\n");
}

TEST(ShaderVariantTest, RoundStrokeRectGP) {
  auto variants = RoundStrokeRectGeometryProcessor::EnumerateVariants();
  CheckVariantListInvariants(variants, 8, "RoundStrokeRectGP");
  // Index 7 = all three set.
  EXPECT_EQ(variants[7].preamble,
            "#define TGFX_GP_RRECT_COMMON_COLOR 1\n"
            "#define TGFX_GP_RRECT_COVERAGE_AA 1\n"
            "#define TGFX_GP_RRECT_UV_MATRIX 1\n");
}

TEST(ShaderVariantTest, ShapeInstancedGP) {
  auto variants = ShapeInstancedGeometryProcessor::EnumerateVariants();
  CheckVariantListInvariants(variants, 4, "ShapeInstancedGP");
  // Index 3 = coverageAA=1, colors=1.
  EXPECT_EQ(variants[3].preamble,
            "#define TGFX_GP_SHAPE_COVERAGE_AA 1\n"
            "#define TGFX_GP_SHAPE_VERTEX_COLORS 1\n");
}

TEST(ShaderVariantTest, AtlasTextGP) {
  auto variants = AtlasTextGeometryProcessor::EnumerateVariants();
  CheckVariantListInvariants(variants, 8, "AtlasTextGP");
  // Index 7 = all three set.
  EXPECT_EQ(variants[7].preamble,
            "#define TGFX_GP_ATLAS_ALPHA_ONLY 1\n"
            "#define TGFX_GP_ATLAS_COMMON_COLOR 1\n"
            "#define TGFX_GP_ATLAS_COVERAGE_AA 1\n");
}

// Aggregate sanity check: total variant count across all GPs matches expectations.
TEST(ShaderVariantTest, AllGPsAggregateCount) {
  size_t total = 0;
  total += DefaultGeometryProcessor::EnumerateVariants().size();
  total += EllipseGeometryProcessor::EnumerateVariants().size();
  total += HairlineLineGeometryProcessor::EnumerateVariants().size();
  total += HairlineQuadGeometryProcessor::EnumerateVariants().size();
  total += MeshGeometryProcessor::EnumerateVariants().size();
  total += NonAARRectGeometryProcessor::EnumerateVariants().size();
  total += QuadPerEdgeAAGeometryProcessor::EnumerateVariants().size();
  total += RoundStrokeRectGeometryProcessor::EnumerateVariants().size();
  total += ShapeInstancedGeometryProcessor::EnumerateVariants().size();
  total += AtlasTextGeometryProcessor::EnumerateVariants().size();
  // 2 + 4 + 2 + 2 + 8 + 4 + 16 + 8 + 4 + 8 = 58.
  EXPECT_EQ(total, 58u);
}

// ---- Fragment Processor variants ----

TEST(ShaderVariantTest, LinearGradientLayout) {
  auto variants = LinearGradientLayout::EnumerateVariants();
  CheckVariantListInvariants(variants, 2, "LinearGradientLayout");
  EXPECT_EQ(variants[0].preamble, "");
  EXPECT_EQ(variants[1].preamble, "#define TGFX_LGRAD_PERSPECTIVE 1\n");
}

TEST(ShaderVariantTest, RadialGradientLayout) {
  auto variants = RadialGradientLayout::EnumerateVariants();
  CheckVariantListInvariants(variants, 2, "RadialGradientLayout");
  EXPECT_EQ(variants[1].preamble, "#define TGFX_RGRAD_PERSPECTIVE 1\n");
}

TEST(ShaderVariantTest, ConicGradientLayout) {
  auto variants = ConicGradientLayout::EnumerateVariants();
  CheckVariantListInvariants(variants, 2, "ConicGradientLayout");
  EXPECT_EQ(variants[1].preamble, "#define TGFX_CGRAD_PERSPECTIVE 1\n");
}

TEST(ShaderVariantTest, DiamondGradientLayout) {
  auto variants = DiamondGradientLayout::EnumerateVariants();
  CheckVariantListInvariants(variants, 2, "DiamondGradientLayout");
  EXPECT_EQ(variants[1].preamble, "#define TGFX_DGRAD_PERSPECTIVE 1\n");
}

TEST(ShaderVariantTest, DeviceSpaceTextureEffect) {
  auto variants = DeviceSpaceTextureEffect::EnumerateVariants();
  CheckVariantListInvariants(variants, 2, "DeviceSpaceTextureEffect");
  EXPECT_EQ(variants[1].preamble, "#define TGFX_DSTE_ALPHA_ONLY 1\n");
}

TEST(ShaderVariantTest, UnrolledBinaryGradientColorizer) {
  auto variants = UnrolledBinaryGradientColorizer::EnumerateVariants();
  CheckVariantListInvariants(variants,
                             static_cast<size_t>(UnrolledBinaryGradientColorizer::MaxIntervalCount),
                             "UnrolledBinaryGradientColorizer");
  // Index 0 corresponds to intervalCount=1.
  EXPECT_EQ(variants[0].preamble, "#define TGFX_UBGC_INTERVAL_COUNT 1\n");
  // Last index covers the maximum supported intervalCount.
  EXPECT_EQ(variants.back().preamble, "#define TGFX_UBGC_INTERVAL_COUNT 8\n");
}

TEST(ShaderVariantTest, GaussianBlur1D) {
  auto variants = GaussianBlur1DFragmentProcessor::EnumerateVariants();
  CheckVariantListInvariants(
      variants, static_cast<size_t>(GaussianBlur1DFragmentProcessor::MaxSupportedMaxSigma),
      "GaussianBlur1D");
  // Index 0 -> maxSigma=1 -> TGFX_BLUR_LOOP_LIMIT = 4*1 = 4.
  EXPECT_EQ(variants[0].preamble, "#define TGFX_BLUR_LOOP_LIMIT 4\n");
  // Last index -> maxSigma=10 -> 40.
  EXPECT_EQ(variants.back().preamble, "#define TGFX_BLUR_LOOP_LIMIT 40\n");
}

TEST(ShaderVariantTest, ColorSpaceXformEffect_Count) {
  auto variants = ColorSpaceXformEffect::EnumerateVariants();
  // 2^5 bool dims (unPremul, srcOOTF, gamut, dstOOTF, premul) x 8 srcTF x 8 dstTF = 2048.
  EXPECT_EQ(variants.size(), static_cast<size_t>(ColorSpaceXformEffect::kVariantCount));
  EXPECT_EQ(variants.size(), 2048u);
}

TEST(ShaderVariantTest, ColorSpaceXformEffect_BaseCaseEmpty) {
  auto variants = ColorSpaceXformEffect::EnumerateVariants();
  // Index 0: all bits zero -> no macros defined -> empty preamble.
  EXPECT_EQ(variants[0].preamble, "");
  EXPECT_EQ(variants[0].index, 0);
}

TEST(ShaderVariantTest, ColorSpaceXformEffect_HashUniqueness) {
  auto variants = ColorSpaceXformEffect::EnumerateVariants();
  std::unordered_set<uint64_t> seen;
  seen.reserve(variants.size());
  for (const auto& v : variants) {
    auto inserted = seen.insert(v.runtimeKeyHash).second;
    EXPECT_TRUE(inserted) << "Duplicate hash at " << v.name;
  }
  EXPECT_EQ(seen.size(), variants.size());
}

// Aggregate sanity check: total variant count across all FPs matches expectations.
TEST(ShaderVariantTest, AllFPsAggregateCount) {
  size_t total = 0;
  total += LinearGradientLayout::EnumerateVariants().size();
  total += RadialGradientLayout::EnumerateVariants().size();
  total += ConicGradientLayout::EnumerateVariants().size();
  total += DiamondGradientLayout::EnumerateVariants().size();
  total += DeviceSpaceTextureEffect::EnumerateVariants().size();
  total += UnrolledBinaryGradientColorizer::EnumerateVariants().size();
  total += GaussianBlur1DFragmentProcessor::EnumerateVariants().size();
  total += ColorSpaceXformEffect::EnumerateVariants().size();
  // 2 + 2 + 2 + 2 + 2 + 8 + 10 + 2048 = 2076.
  EXPECT_EQ(total, 2076u);
}

}  // namespace tgfx

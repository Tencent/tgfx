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

#include "base/TGFXTest.h"
#include "gpu/PrecompiledShaderCache.h"
#include "gpu/shaders/PrecompiledShader.h"
#include "gpu/shaders/ShaderPermutation.h"
#include "gpu/shaders/level1/TextureFillShader.h"
#include "gtest/gtest.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/Surface.h"
#include "utils/TestUtils.h"

namespace tgfx {

TGFX_TEST(ShaderPermutationTest, DimTypes) {
  PermutationBool boolDim("HAS_YUV");
  EXPECT_EQ(PermutationBool::valueCount(), 2);
  EXPECT_STREQ(boolDim.defineName, "HAS_YUV");

  PermutationEnum enumDim("MODE", {"NONE", "CLAMP", "REPEAT"});
  EXPECT_EQ(enumDim.valueCount(), 3);
  EXPECT_STREQ(enumDim.defineName, "MODE");
  EXPECT_STREQ(enumDim.valueNames[1], "CLAMP");

  PermutationInt intDim("OCTAVES", 5);
  EXPECT_EQ(intDim.valueCount(), 5);
  EXPECT_STREQ(intDim.defineName, "OCTAVES");
}

TGFX_TEST(ShaderPermutationTest, DomainEncodeDecodeAllBool) {
  // Test 2 dimensions (4 combinations)
  {
    auto domain = PermutationDomain::FromBoolNames("A, B");
    EXPECT_EQ(domain.totalCount(), 4u);
    EXPECT_EQ(domain.dimensionCount(), 2u);
    for (uint32_t i = 0; i < domain.totalCount(); i++) {
      auto values = domain.decode(i);
      EXPECT_EQ(domain.encode(values), i);
    }
  }
  // Test 4 dimensions (16 combinations)
  {
    auto domain = PermutationDomain::FromBoolNames("A, B, C, D");
    EXPECT_EQ(domain.totalCount(), 16u);
    for (uint32_t i = 0; i < domain.totalCount(); i++) {
      auto values = domain.decode(i);
      EXPECT_EQ(domain.encode(values), i);
    }
  }
  // Test 8 dimensions (256 combinations)
  {
    auto domain = PermutationDomain::FromBoolNames("A, B, C, D, E, F, G, H");
    EXPECT_EQ(domain.totalCount(), 256u);
    for (uint32_t i = 0; i < domain.totalCount(); i++) {
      auto values = domain.decode(i);
      EXPECT_EQ(domain.encode(values), i);
    }
  }
}

TGFX_TEST(ShaderPermutationTest, DomainWithEnumAndInt) {
  // Domain: enum(3 values) + bool
  std::vector<PermutationDimension> dims;
  dims.emplace_back(PermutationEnum("MODE", {"NONE", "CLAMP", "REPEAT"}));
  dims.emplace_back(PermutationBool("HAS_ALPHA"));
  PermutationDomain domain(std::move(dims));
  EXPECT_EQ(domain.totalCount(), 6u);

  // index=0: MODE=0, HAS_ALPHA=0
  auto v0 = domain.decode(0);
  EXPECT_EQ(v0[0], 0);
  EXPECT_EQ(v0[1], 0);

  // index=1: MODE=1, HAS_ALPHA=0
  auto v1 = domain.decode(1);
  EXPECT_EQ(v1[0], 1);
  EXPECT_EQ(v1[1], 0);

  // index=3: MODE=0, HAS_ALPHA=1
  auto v3 = domain.decode(3);
  EXPECT_EQ(v3[0], 0);
  EXPECT_EQ(v3[1], 1);

  // index=5: MODE=2, HAS_ALPHA=1
  auto v5 = domain.decode(5);
  EXPECT_EQ(v5[0], 2);
  EXPECT_EQ(v5[1], 1);

  // Round-trip all
  for (uint32_t i = 0; i < domain.totalCount(); i++) {
    EXPECT_EQ(domain.encode(domain.decode(i)), i);
  }
}

TGFX_TEST(ShaderPermutationTest, DefineListForZeroValue) {
  auto domain = PermutationDomain::FromBoolNames("X, Y, Z");
  auto defines = domain.defineListFor(0);
  EXPECT_EQ(defines.size(), 3u);
  EXPECT_EQ(defines[0], "X=0");
  EXPECT_EQ(defines[1], "Y=0");
  EXPECT_EQ(defines[2], "Z=0");

  // Also verify a non-zero index
  // index=5 for 3-bool: decode(5) = {1, 0, 1} (5 = 1*1 + 0*2 + 1*4)
  auto defines5 = domain.defineListFor(5);
  EXPECT_EQ(defines5[0], "X=1");
  EXPECT_EQ(defines5[1], "Y=0");
  EXPECT_EQ(defines5[2], "Z=1");
}

TGFX_TEST(ShaderPermutationTest, DefineDimsMacro) {
  using D = TextureFillShader::Dims;
  EXPECT_EQ(D::COUNT, 4u);
  EXPECT_EQ(D::HAS_YUV, 0u);
  EXPECT_EQ(D::ALPHA_ONLY, 1u);
  EXPECT_EQ(D::HAS_RGBAAA, 2u);
  EXPECT_EQ(D::HAS_SUBSET, 3u);

  auto domain = D::domain();
  EXPECT_EQ(domain.dimensionCount(), static_cast<size_t>(D::COUNT));
  EXPECT_EQ(domain.totalCount(), 16u);

  // Verify domain dimensions match
  auto handDomain = PermutationDomain::FromBoolNames("HAS_YUV, ALPHA_ONLY, HAS_RGBAAA, HAS_SUBSET");
  EXPECT_EQ(handDomain.totalCount(), domain.totalCount());
  EXPECT_EQ(handDomain.dimensionCount(), domain.dimensionCount());

  // Verify encode/decode match between the two
  for (uint32_t i = 0; i < domain.totalCount(); i++) {
    auto v = domain.decode(i);
    EXPECT_EQ(handDomain.encode(v), i);
  }
}

TGFX_TEST(ShaderPermutationTest, ShaderRegistry) {
  auto& factories = ShaderRegistry::All();
  bool foundTextureFill = false;
  for (auto& factory : factories) {
    auto shader = factory();
    auto shaderInfo = shader->info();
    if (shaderInfo.name == "TextureFillShader") {
      foundTextureFill = true;
      EXPECT_EQ(shaderInfo.domain.totalCount(), 16u);
      EXPECT_EQ(shaderInfo.domain.dimensionCount(), 4u);
      EXPECT_EQ(shaderInfo.vertexFile, "level1/texture_fill.vert");
      EXPECT_EQ(shaderInfo.fragmentFile, "level1/texture_fill.frag");
    }
  }
  EXPECT_TRUE(foundTextureFill);
}

TGFX_TEST(ShaderPermutationTest, ShouldCompile) {
  auto& factories = ShaderRegistry::All();
  for (auto& factory : factories) {
    auto shader = factory();
    auto shaderInfo = shader->info();
    if (shaderInfo.name != "TextureFillShader") {
      continue;
    }
    // 4 bool dims = 16 raw combinations. ShouldCompile excludes hasYuv && (alphaOnly || hasRgbaaa).
    // hasYuv=1 && alphaOnly=1: 4 combos (hasRgbaaa=0/1, hasSubset=0/1) but 2 overlap with below
    // hasYuv=1 && hasRgbaaa=1: 4 combos
    // Union: hasYuv=1 && (alphaOnly=1 || hasRgbaaa=1) = 6 combos excluded
    int compiledCount = 0;
    for (uint32_t i = 0; i < shaderInfo.domain.totalCount(); i++) {
      auto values = shaderInfo.domain.decode(i);
      if (shaderInfo.shouldCompile(values)) {
        compiledCount++;
      }
    }
    EXPECT_EQ(compiledCount, 6);
  }
}

TGFX_TEST(ShaderPermutationTest, EncodeWithBitShift) {
  // Verify that 1u << D::DIM_NAME produces the same result as domain.encode({...})
  // for an all-bool domain (each dim contributes a single bit).
  using D = TextureFillShader::Dims;
  auto domain = D::domain();

  // HAS_YUV=1, others=0 -> index = 1<<0 = 1
  EXPECT_EQ(domain.encode({1, 0, 0, 0}), 1u << D::HAS_YUV);
  // ALPHA_ONLY=1, others=0 -> index = 1<<1 = 2
  EXPECT_EQ(domain.encode({0, 1, 0, 0}), 1u << D::ALPHA_ONLY);
  // HAS_RGBAAA=1, others=0 -> index = 1<<2 = 4
  EXPECT_EQ(domain.encode({0, 0, 1, 0}), 1u << D::HAS_RGBAAA);
  // HAS_SUBSET=1, others=0 -> index = 1<<3 = 8
  EXPECT_EQ(domain.encode({0, 0, 0, 1}), 1u << D::HAS_SUBSET);
  // HAS_YUV=1, HAS_SUBSET=1 -> index = (1<<0) | (1<<3) = 9
  EXPECT_EQ(domain.encode({1, 0, 0, 1}), (1u << D::HAS_YUV) | (1u << D::HAS_SUBSET));
}

TGFX_TEST(ShaderPermutationTest, PrecompiledBundleLoad) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto bundlePath = ProjectPath::Absolute("resources/shaders/shader_bundle.vulkan.bin");
  auto* cache = context->precompiledShaderCache();
  ASSERT_TRUE(cache->loadBundle(bundlePath));
  EXPECT_EQ(cache->entryCount(), 6u);
  EXPECT_EQ(cache->profileTag(), "vulkan");
}

TGFX_TEST(ShaderPermutationTest, PrecompiledRender) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto bundlePath = ProjectPath::Absolute("resources/shaders/shader_bundle.vulkan.bin");
  auto* cache = context->precompiledShaderCache();
  ASSERT_TRUE(cache->loadBundle(bundlePath));

  auto image = MakeImage("resources/apitest/test_timestretch.png");
  ASSERT_TRUE(image != nullptr);
  auto surface = Surface::Make(context, 200, 200);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();
  canvas->drawImage(image, 0, 0);
  EXPECT_TRUE(Baseline::Compare(surface, "ShaderPermutationTest/PrecompiledRender"));
}

}  // namespace tgfx

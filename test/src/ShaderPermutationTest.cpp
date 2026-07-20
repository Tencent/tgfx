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

#include <chrono>
#include <fstream>
#include <vector>
#include "base/TGFXTest.h"
#include "core/filters/GaussianBlurImageFilter.h"
#include "gpu/GlobalCache.h"
#include "gpu/PrecompiledShaderCache.h"
#include "gpu/shaders/PrecompiledShader.h"
#include "gpu/shaders/ShaderPermutation.h"
#include "gpu/shaders/level1/QuadTextureFillShader.h"
#include "gpu/shaders/level1/TextureFillShader.h"
#include "gtest/gtest.h"
#include "tgfx/core/ColorFilter.h"
#include "tgfx/core/ColorSpace.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Shader.h"
#include "tgfx/core/Surface.h"
#include "utils/TestUtils.h"
#include "zlib.h"

namespace tgfx {

#ifndef TGFX_BACKEND_NAME
#define TGFX_BACKEND_NAME "opengl"
#endif

static std::string BundlePath() {
  std::string backend = TGFX_BACKEND_NAME;
  auto pos = backend.find('-');
  if (pos != std::string::npos) {
    backend = backend.substr(0, pos);
  }
  return "resources/shaders/shader_bundle." + backend + ".bin";
}

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
      EXPECT_EQ(shaderInfo.vertDomain.totalCount(), 16u);
      EXPECT_EQ(shaderInfo.vertDomain.dimensionCount(), 4u);
      // FragDims: 4 bools + 1 int(3) = 2^4 * 3 = 48 total permutations
      EXPECT_EQ(shaderInfo.fragDomain.totalCount(), 48u);
      EXPECT_EQ(shaderInfo.fragDomain.dimensionCount(), 5u);
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
    // Vert: 4 bool = 16 raw. Frag: 4 bool + HAS_XP(int3) = 48 raw.
    // ShouldCompile rules:
    //   - HAS_YUV(frag) != 0 → excluded (YUV falls back to ProgramBuilder)
    //   - ALPHA_ONLY && HAS_RGBAAA both set → excluded (mutually exclusive)
    //   - Vert 4 dims must match frag first 4 dims
    // Valid vert configs (HAS_YUV=0): 8, minus ALPHA_ONLY=1&&HAS_RGBAAA=1 = 6.
    // Each valid vert matches 3 frag variants (HAS_XP=0,1,2) → 6 * 3 = 18.
    int compiledCount = 0;
    for (uint32_t vi = 0; vi < shaderInfo.vertDomain.totalCount(); vi++) {
      auto vertValues = shaderInfo.vertDomain.decode(vi);
      for (uint32_t fi = 0; fi < shaderInfo.fragDomain.totalCount(); fi++) {
        auto fragValues = shaderInfo.fragDomain.decode(fi);
        if (shaderInfo.shouldCompile(vi, fi, vertValues, fragValues)) {
          compiledCount++;
        }
      }
    }
    EXPECT_EQ(compiledCount, 18);
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
  auto bundlePath = ProjectPath::Absolute(BundlePath());
  auto* cache = context->precompiledShaderCache();
  ASSERT_TRUE(cache->loadBundle(bundlePath));
  EXPECT_EQ(cache->vertexEntryCount(), 124u);
  EXPECT_EQ(cache->fragmentEntryCount(), 1005u);
  std::string expectedTag = TGFX_BACKEND_NAME;
  auto dashPos = expectedTag.find('-');
  if (dashPos != std::string::npos) {
    expectedTag = expectedTag.substr(0, dashPos);
  }
  EXPECT_EQ(cache->profileTag(), expectedTag);
  cache->unload();
}

TGFX_TEST(ShaderPermutationTest, PrecompiledPerformance) {
  auto image = MakeImage("resources/apitest/test_timestretch.png");
  ASSERT_TRUE(image != nullptr);
  int width = 200;
  int height = 200;

  // Measure ProgramBuilder path (no bundle).
  auto startBuilder = std::chrono::steady_clock::now();
  {
    ContextScope scope;
    auto context = scope.getContext();
    ASSERT_TRUE(context != nullptr);
    auto surface = Surface::Make(context, width, height);
    ASSERT_TRUE(surface != nullptr);
    surface->getCanvas()->drawImage(image, 0, 0);
    context->flushAndSubmit(true);
  }
  auto endBuilder = std::chrono::steady_clock::now();

  // Measure PrecompiledProgramCreator path (with bundle).
  auto startPrecompiled = std::chrono::steady_clock::now();
  {
    ContextScope scope;
    auto context = scope.getContext();
    ASSERT_TRUE(context != nullptr);
    auto* cache = context->precompiledShaderCache();
    auto bundlePath = ProjectPath::Absolute(BundlePath());
    ASSERT_TRUE(cache->loadBundle(bundlePath));
    auto surface = Surface::Make(context, width, height);
    ASSERT_TRUE(surface != nullptr);
    surface->getCanvas()->drawImage(image, 0, 0);
    context->flushAndSubmit(true);
    cache->unload();
  }
  auto endPrecompiled = std::chrono::steady_clock::now();

  auto builderUs =
      std::chrono::duration_cast<std::chrono::microseconds>(endBuilder - startBuilder).count();
  auto precompiledUs =
      std::chrono::duration_cast<std::chrono::microseconds>(endPrecompiled - startPrecompiled)
          .count();
  printf("  ProgramBuilder path:  %lld us\n", static_cast<long long>(builderUs));
  printf("  Precompiled path:     %lld us\n", static_cast<long long>(precompiledUs));
  printf("  Speedup:              %.2fx\n",
         static_cast<double>(builderUs) / static_cast<double>(precompiledUs));
}

TGFX_TEST(ShaderPermutationTest, PrecompiledRenderConsistency) {
  auto image = MakeImage("resources/apitest/test_timestretch.png");
  ASSERT_TRUE(image != nullptr);
  int width = 200;
  int height = 200;

  // Pass 1: render with precompiled bundle loaded (PrecompiledProgramCreator path).
  Bitmap bitmap1;
  bitmap1.allocPixels(width, height);
  {
    ContextScope scope;
    auto context = scope.getContext();
    ASSERT_TRUE(context != nullptr);
    auto* cache = context->precompiledShaderCache();
    auto bundlePath = ProjectPath::Absolute(BundlePath());
    ASSERT_TRUE(cache->loadBundle(bundlePath));
    auto surface = Surface::Make(context, width, height);
    ASSERT_TRUE(surface != nullptr);
    surface->getCanvas()->drawImage(image, 0, 0);
    auto* pixels = bitmap1.lockPixels();
    ASSERT_TRUE(pixels != nullptr);
    ASSERT_TRUE(surface->readPixels(bitmap1.info(), pixels));
    bitmap1.unlockPixels();
    cache->unload();
  }

  // Pass 2: render without bundle (ProgramBuilder path).
  Bitmap bitmap2;
  bitmap2.allocPixels(width, height);
  {
    ContextScope scope;
    auto context = scope.getContext();
    ASSERT_TRUE(context != nullptr);
    auto surface = Surface::Make(context, width, height);
    ASSERT_TRUE(surface != nullptr);
    surface->getCanvas()->drawImage(image, 0, 0);
    auto* pixels = bitmap2.lockPixels();
    ASSERT_TRUE(pixels != nullptr);
    ASSERT_TRUE(surface->readPixels(bitmap2.info(), pixels));
    bitmap2.unlockPixels();
  }

  // Both paths must produce identical output.
  size_t totalBytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
  auto* p1 = bitmap1.lockPixels();
  auto* p2 = bitmap2.lockPixels();
  EXPECT_EQ(memcmp(p1, p2, totalBytes), 0);
  bitmap1.unlockPixels();
  bitmap2.unlockPixels();
}

TGFX_TEST(ShaderPermutationTest, ShaderCacheStats) {
  PrecompiledShaderCache cache;

  // Initial state: all counters are zero.
  EXPECT_EQ(cache.hitCount(), 0u);
  EXPECT_EQ(cache.missCount(), 0u);

  // Record artifact hits and misses.
  cache.recordHit();
  cache.recordHit();
  cache.recordArtifactMiss(PrecompiledFallbackReason::NoPermutationMatch);
  EXPECT_EQ(cache.hitCount(), 2u);
  EXPECT_EQ(cache.missCount(), 1u);
  EXPECT_EQ(cache.fallbackCount(PrecompiledFallbackReason::NoPermutationMatch), 1u);
  EXPECT_EQ(cache.fallbackCount(PrecompiledFallbackReason::VertexArtifactMissing), 0u);

  // Verify resetStats clears all counters.
  cache.resetStats();
  EXPECT_EQ(cache.hitCount(), 0u);
  EXPECT_EQ(cache.missCount(), 0u);
  EXPECT_EQ(cache.fallbackCount(PrecompiledFallbackReason::NoPermutationMatch), 0u);

  // Record after reset should start fresh.
  cache.recordArtifactMiss(PrecompiledFallbackReason::VertexArtifactMissing);
  cache.recordFailure(PrecompiledFallbackReason::VertexModuleCreationFailed);
  cache.recordFailure(PrecompiledFallbackReason::PipelineCreationFailed);
  cache.recordHit();
  EXPECT_EQ(cache.hitCount(), 1u);
  EXPECT_EQ(cache.missCount(), 1u);
  EXPECT_EQ(cache.fallbackCount(PrecompiledFallbackReason::VertexArtifactMissing), 1u);
  EXPECT_EQ(cache.fallbackCount(PrecompiledFallbackReason::VertexModuleCreationFailed), 1u);
  EXPECT_EQ(cache.fallbackCount(PrecompiledFallbackReason::PipelineCreationFailed), 1u);
}

TGFX_TEST(ShaderPermutationTest, ProgramProvenanceSurvivesCacheHits) {
  GlobalCache cache(nullptr);
  BytesKey precompiledKey;
  precompiledKey.write(1u);
  ProgramProvenance precompiledProvenance = {ShaderArtifactOrigin::OfflineBinary,
                                             ProgramOrigin::PrecompiledArtifact,
                                             PipelineOrigin::RuntimeCreation};
  auto precompiledProgram =
      std::make_shared<Program>(nullptr, nullptr, nullptr, precompiledProvenance);
  cache.addProgram(precompiledKey, precompiledProgram);

  BytesKey dynamicKey;
  dynamicKey.write(2u);
  ProgramProvenance dynamicProvenance = {ShaderArtifactOrigin::RuntimeGeneratedSource,
                                         ProgramOrigin::ProgramBuilder,
                                         PipelineOrigin::RuntimeCreation};
  auto dynamicProgram = std::make_shared<Program>(nullptr, nullptr, nullptr, dynamicProvenance);
  cache.addProgram(dynamicKey, dynamicProgram);

  auto cachedPrecompiled = cache.findProgram(precompiledKey);
  auto cachedDynamic = cache.findProgram(dynamicKey);
  ASSERT_TRUE(cachedPrecompiled != nullptr);
  ASSERT_TRUE(cachedDynamic != nullptr);
  EXPECT_EQ(cachedPrecompiled->getProvenance().program, ProgramOrigin::PrecompiledArtifact);
  EXPECT_EQ(cachedDynamic->getProvenance().program, ProgramOrigin::ProgramBuilder);
  EXPECT_EQ(cachedPrecompiled->getProvenance().shaderArtifact, ShaderArtifactOrigin::OfflineBinary);
  EXPECT_EQ(cachedDynamic->getProvenance().shaderArtifact,
            ShaderArtifactOrigin::RuntimeGeneratedSource);

  cache.recordRuntimePipelineCreation(true);
  cache.recordRuntimePipelineCreation(true);
  cache.recordRuntimePipelineCreation(false);
  const auto& stats = cache.programStats();
  EXPECT_EQ(stats.requests, 2u);
  EXPECT_EQ(stats.cacheHits, 2u);
  EXPECT_EQ(stats.cacheMisses, 0u);
  EXPECT_EQ(stats.precompiledArtifactCreations, 1u);
  EXPECT_EQ(stats.programBuilderCreations, 1u);
  EXPECT_EQ(stats.runtimePipelineCreationAttempts, 3u);
  EXPECT_EQ(stats.runtimePipelineCreationSuccesses, 2u);
  EXPECT_EQ(stats.runtimePipelineCreationFailures, 1u);
}

TGFX_TEST(ShaderPermutationTest, EmbeddedBundleLoadFromMemory) {
  // Verify that PrecompiledShaderCache can load a bundle from in-memory data (the same interface
  // used by the embedded bundle mechanism in Context initialization).
  auto bundlePath = ProjectPath::Absolute(BundlePath());
  std::ifstream file(bundlePath, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    GTEST_SKIP() << "Bundle file not found, skipping embedded load test";
    return;
  }
  auto fileSize = static_cast<size_t>(file.tellg());
  file.seekg(0);
  std::vector<uint8_t> data(fileSize);
  file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(fileSize));
  file.close();

  // Load from memory (simulates embedded bundle)
  PrecompiledShaderCache cache;
  ASSERT_TRUE(cache.loadBundle(data.data(), data.size()));
  EXPECT_TRUE(cache.isLoaded());
  EXPECT_GT(cache.vertexEntryCount(), 0u);
  EXPECT_GT(cache.fragmentEntryCount(), 0u);
  std::string expectedTag2 = TGFX_BACKEND_NAME;
  auto dashPos2 = expectedTag2.find('-');
  if (dashPos2 != std::string::npos) {
    expectedTag2 = expectedTag2.substr(0, dashPos2);
  }
  EXPECT_EQ(cache.profileTag(), expectedTag2);
}

TGFX_TEST(ShaderPermutationTest, EmbeddedBundleInvalidData) {
  // Verify that loadBundle rejects invalid data gracefully.
  PrecompiledShaderCache cache;

  // Empty data
  EXPECT_FALSE(cache.loadBundle(nullptr, 0));

  // Too small
  uint8_t tooSmall[10] = {0};
  EXPECT_FALSE(cache.loadBundle(tooSmall, sizeof(tooSmall)));

  // Wrong magic
  std::vector<uint8_t> badMagic(80, 0);
  badMagic[0] = 0xFF;
  EXPECT_FALSE(cache.loadBundle(badMagic.data(), badMagic.size()));
}

static uint16_t TestReadU16LE(const uint8_t* p) {
  return static_cast<uint16_t>(static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8));
}

static uint32_t TestReadU32LE(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

static void TestWriteU16LE(uint8_t* p, uint16_t val) {
  p[0] = static_cast<uint8_t>(val & 0xFF);
  p[1] = static_cast<uint8_t>((val >> 8) & 0xFF);
}

static void TestWriteU32LE(uint8_t* p, uint32_t val) {
  p[0] = static_cast<uint8_t>(val & 0xFF);
  p[1] = static_cast<uint8_t>((val >> 8) & 0xFF);
  p[2] = static_cast<uint8_t>((val >> 16) & 0xFF);
  p[3] = static_cast<uint8_t>((val >> 24) & 0xFF);
}

static std::vector<uint8_t> MakeTestBundle(const std::string& profileTag, uint32_t vertCount,
                                           uint32_t fragCount, uint32_t hashSeed) {
  constexpr uint32_t HeaderSize = 80;
  constexpr uint32_t PoolEntrySize = 28;
  auto fragPoolOffset = HeaderSize + vertCount * PoolEntrySize;
  auto dataOffset = fragPoolOffset + fragCount * PoolEntrySize;
  auto dataSize = vertCount + fragCount;
  std::vector<uint8_t> bundle(dataOffset + dataSize, 0);
  TestWriteU32LE(bundle.data(), 0x54475346);
  TestWriteU16LE(bundle.data() + 4, 3);
  TestWriteU32LE(bundle.data() + 20, vertCount);
  TestWriteU32LE(bundle.data() + 24, fragCount);
  TestWriteU32LE(bundle.data() + 28, HeaderSize);
  TestWriteU32LE(bundle.data() + 32, fragPoolOffset);
  TestWriteU32LE(bundle.data() + 36, dataOffset);
  TestWriteU32LE(bundle.data() + 40, dataSize);
  for (size_t index = 0; index < profileTag.size() && index < 31; ++index) {
    bundle[48 + index] = static_cast<uint8_t>(profileTag[index]);
  }
  for (uint32_t index = 0; index < vertCount; ++index) {
    auto entryOffset = HeaderSize + index * PoolEntrySize;
    TestWriteU32LE(bundle.data() + entryOffset, hashSeed + index);
    TestWriteU32LE(bundle.data() + entryOffset + 16, index);
    TestWriteU32LE(bundle.data() + entryOffset + 20, 1);
    bundle[dataOffset + index] = static_cast<uint8_t>(index + 1);
  }
  for (uint32_t index = 0; index < fragCount; ++index) {
    auto entryOffset = fragPoolOffset + index * PoolEntrySize;
    TestWriteU32LE(bundle.data() + entryOffset, hashSeed + 0x100 + index);
    TestWriteU32LE(bundle.data() + entryOffset + 16, vertCount + index);
    TestWriteU32LE(bundle.data() + entryOffset + 20, 1);
    bundle[dataOffset + vertCount + index] = static_cast<uint8_t>(index + 1);
  }
  return bundle;
}

TGFX_TEST(ShaderPermutationTest, CompressedBundleRejectsInvalidOffsetOrder) {
  std::vector<uint8_t> bundle(80, 0);
  TestWriteU32LE(bundle.data(), 0x54475346);
  TestWriteU16LE(bundle.data() + 4, 3);
  TestWriteU16LE(bundle.data() + 6, 1);
  TestWriteU32LE(bundle.data() + 36, 80);
  TestWriteU32LE(bundle.data() + 40, 1);
  TestWriteU32LE(bundle.data() + 44, 64);

  PrecompiledShaderCache cache;
  EXPECT_FALSE(cache.loadBundle(bundle.data(), bundle.size()));
  EXPECT_FALSE(cache.isLoaded());
}

TGFX_TEST(ShaderPermutationTest, FailedBundleReloadPreservesCache) {
  auto original = MakeTestBundle("original", 1, 1, 10);
  PrecompiledShaderCache cache;
  ASSERT_TRUE(cache.loadBundle(original.data(), original.size()));
  ASSERT_TRUE(cache.findVertex(10, 0) != nullptr);
  ASSERT_TRUE(cache.findFragment(10 + 0x100, 0) != nullptr);

  auto invalid = MakeTestBundle("failed", 2, 1, 30);
  TestWriteU32LE(invalid.data() + 32, static_cast<uint32_t>(invalid.size() - 1));
  EXPECT_FALSE(cache.loadBundle(invalid.data(), invalid.size()));
  EXPECT_EQ(cache.vertexEntryCount(), 1u);
  EXPECT_EQ(cache.fragmentEntryCount(), 1u);
  EXPECT_EQ(cache.profileTag(), "original");
  EXPECT_TRUE(cache.findVertex(10, 0) != nullptr);
  EXPECT_TRUE(cache.findFragment(10 + 0x100, 0) != nullptr);
  EXPECT_TRUE(cache.findVertex(30, 0) == nullptr);
}

TGFX_TEST(ShaderPermutationTest, SuccessfulBundleReloadReplacesCache) {
  auto original = MakeTestBundle("original", 2, 2, 10);
  PrecompiledShaderCache cache;
  ASSERT_TRUE(cache.loadBundle(original.data(), original.size()));
  ASSERT_EQ(cache.vertexEntryCount(), 2u);
  ASSERT_EQ(cache.fragmentEntryCount(), 2u);

  auto replacement = MakeTestBundle("replacement", 1, 1, 30);
  ASSERT_TRUE(cache.loadBundle(replacement.data(), replacement.size()));
  EXPECT_TRUE(cache.isLoaded());
  EXPECT_EQ(cache.vertexEntryCount(), 1u);
  EXPECT_EQ(cache.fragmentEntryCount(), 1u);
  EXPECT_EQ(cache.profileTag(), "replacement");
  EXPECT_TRUE(cache.findVertex(30, 0) != nullptr);
  EXPECT_TRUE(cache.findFragment(30 + 0x100, 0) != nullptr);
  EXPECT_TRUE(cache.findVertex(10, 0) == nullptr);
  EXPECT_TRUE(cache.findFragment(10 + 0x100, 0) == nullptr);
}

TGFX_TEST(ShaderPermutationTest, CompressedBundleLoad) {
  // Load an uncompressed bundle, manually compress its data pool, then verify loading.
  auto bundlePath = ProjectPath::Absolute(BundlePath());
  std::ifstream file(bundlePath, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    GTEST_SKIP() << "Bundle file not found";
    return;
  }
  auto fileSize = static_cast<size_t>(file.tellg());
  file.seekg(0);
  std::vector<uint8_t> original(fileSize);
  file.read(reinterpret_cast<char*>(original.data()), static_cast<std::streamsize>(fileSize));
  file.close();

  // Verify it's uncompressed (compressionType at offset 6).
  ASSERT_EQ(TestReadU16LE(original.data() + 6), 0u);

  uint32_t dataOffset = TestReadU32LE(original.data() + 36);
  uint32_t dataSize = TestReadU32LE(original.data() + 40);
  uint32_t reflectionOffset = TestReadU32LE(original.data() + 44);
  ASSERT_GT(dataSize, 0u);

  // Compress the data pool section.
  uLongf compBound = compressBound(static_cast<uLong>(dataSize));
  std::vector<uint8_t> compressedData(compBound);
  uLongf compSize = compBound;
  int ret = compress2(compressedData.data(), &compSize, original.data() + dataOffset,
                      static_cast<uLong>(dataSize), Z_BEST_COMPRESSION);
  ASSERT_EQ(ret, Z_OK);
  compressedData.resize(compSize);

  // Build a new bundle: [header+pools | compressed data | reflection]
  std::vector<uint8_t> compressed;
  compressed.insert(compressed.end(), original.begin(), original.begin() + dataOffset);
  compressed.insert(compressed.end(), compressedData.begin(), compressedData.end());
  if (reflectionOffset > 0) {
    compressed.insert(compressed.end(), original.begin() + reflectionOffset, original.end());
  }

  // Patch header: set compressionType=1 and update reflectionOffset.
  TestWriteU16LE(compressed.data() + 6, 1);
  if (reflectionOffset > 0) {
    uint32_t newReflOffset = static_cast<uint32_t>(dataOffset + compSize);
    compressed[44] = static_cast<uint8_t>(newReflOffset & 0xFF);
    compressed[45] = static_cast<uint8_t>((newReflOffset >> 8) & 0xFF);
    compressed[46] = static_cast<uint8_t>((newReflOffset >> 16) & 0xFF);
    compressed[47] = static_cast<uint8_t>((newReflOffset >> 24) & 0xFF);
  }

  // Load the compressed bundle.
  PrecompiledShaderCache compressedCache;
  ASSERT_TRUE(compressedCache.loadBundle(compressed.data(), compressed.size()));
  EXPECT_TRUE(compressedCache.isLoaded());

  // Compare with uncompressed load.
  PrecompiledShaderCache originalCache;
  ASSERT_TRUE(originalCache.loadBundle(original.data(), original.size()));
  EXPECT_EQ(compressedCache.vertexEntryCount(), originalCache.vertexEntryCount());
  EXPECT_EQ(compressedCache.fragmentEntryCount(), originalCache.fragmentEntryCount());
  EXPECT_EQ(compressedCache.profileTag(), originalCache.profileTag());

  // Verify compressed size is smaller.
  EXPECT_LT(compressed.size(), original.size());
  printf("  CompressedBundle: %zu -> %zu bytes (%.1f%% reduction)\n", original.size(),
         compressed.size(),
         100.0 *
             (1.0 - static_cast<double>(compressed.size()) / static_cast<double>(original.size())));
}

TGFX_TEST(ShaderPermutationTest, DrawImageHitsPrecompiledCache) {
  auto image = MakeImage("resources/apitest/test_timestretch.png");
  ASSERT_TRUE(image != nullptr);
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  context->globalCache()->clearPrograms();
  auto bundlePath = ProjectPath::Absolute(BundlePath());
  auto* cache = context->precompiledShaderCache();
  ASSERT_TRUE(cache->loadBundle(bundlePath));
  cache->resetStats();
  auto surface = Surface::Make(context, 200, 200);
  ASSERT_TRUE(surface != nullptr);
  surface->getCanvas()->drawImage(image, 0, 0);
  context->flushAndSubmit(true);
  EXPECT_GT(cache->hitCount(), 0u);
  cache->unload();
}

TGFX_TEST(ShaderPermutationTest, AlphaThresholdHitsPrecompiledCache) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto bundlePath = ProjectPath::Absolute(BundlePath());
  auto* cache = context->precompiledShaderCache();
  ASSERT_TRUE(cache->loadBundle(bundlePath));
  cache->resetStats();
  auto surface = Surface::Make(context, 100, 100);
  ASSERT_TRUE(surface != nullptr);
  Paint paint;
  paint.setColor(Color::FromRGBA(100, 0, 0, 128));
  paint.setColorFilter(ColorFilter::AlphaThreshold(0.5f));
  surface->getCanvas()->drawRect(Rect::MakeWH(100, 100), paint);
  context->flushAndSubmit(true);
  EXPECT_GT(cache->hitCount(), 0u);
  cache->unload();
}

TGFX_TEST(ShaderPermutationTest, LumaHitsPrecompiledCache) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto bundlePath = ProjectPath::Absolute(BundlePath());
  auto* cache = context->precompiledShaderCache();
  ASSERT_TRUE(cache->loadBundle(bundlePath));
  cache->resetStats();
  auto surface = Surface::Make(context, 100, 100);
  ASSERT_TRUE(surface != nullptr);
  Paint paint;
  paint.setColor(Color::FromRGBA(125, 0, 255));
  paint.setColorFilter(ColorFilter::Luma());
  surface->getCanvas()->drawRect(Rect::MakeWH(100, 100), paint);
  context->flushAndSubmit(true);
  EXPECT_GT(cache->hitCount(), 0u);
  cache->unload();
}

TGFX_TEST(ShaderPermutationTest, GaussianBlurHitsPrecompiledCache) {
  auto image = MakeImage("resources/apitest/image_as_mask.png");
  ASSERT_TRUE(image != nullptr);
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto bundlePath = ProjectPath::Absolute(BundlePath());
  auto* cache = context->precompiledShaderCache();
  ASSERT_TRUE(cache->loadBundle(bundlePath));
  cache->resetStats();
  int width = image->width() + 50;
  int height = image->height() + 50;
  auto surface = Surface::Make(context, width, height);
  ASSERT_TRUE(surface != nullptr);
  auto blurFilter = std::make_shared<GaussianBlurImageFilter>(3.0f, 3.0f, TileMode::Clamp);
  auto blurredImage = image->makeWithFilter(blurFilter);
  surface->getCanvas()->drawImage(blurredImage, 25, 25);
  context->flushAndSubmit(true);
  EXPECT_GT(cache->hitCount(), 0u);
  cache->unload();
}

// TODO: BlendMerge frag assumes child FP output via texture, but ModeColorFilter inlines the
// child (ConstColorProcessor). Needs EffectDecomposer integration or shader redesign.
TGFX_TEST(ShaderPermutationTest, DISABLED_BlendMergeHitsPrecompiledCache) {
  auto image = MakeImage("resources/apitest/test_timestretch.png");
  ASSERT_TRUE(image != nullptr);
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto bundlePath = ProjectPath::Absolute(BundlePath());
  auto* cache = context->precompiledShaderCache();
  ASSERT_TRUE(cache->loadBundle(bundlePath));
  cache->resetStats();
  auto surface = Surface::Make(context, 200, 200);
  ASSERT_TRUE(surface != nullptr);
  Paint paint;
  paint.setColor(Color::Red());
  paint.setColorFilter(ColorFilter::Blend(Color::Blue(), BlendMode::SrcOver));
  surface->getCanvas()->drawRect(Rect::MakeWH(200, 200), paint);
  context->flushAndSubmit(true);
  EXPECT_GT(cache->hitCount(), 0u);
}

TGFX_TEST(ShaderPermutationTest, BlendMergeClipHitsPrecompiledCache) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto bundlePath = ProjectPath::Absolute(BundlePath());
  auto* cache = context->precompiledShaderCache();
  ASSERT_TRUE(cache->loadBundle(bundlePath));
  cache->resetStats();
  auto surface = Surface::Make(context, 200, 200);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();
  // Draw with ColorFilter::Blend (produces XfermodeFragmentProcessor) + AA clip rect
  // (produces AARectEffect as coverage FP). This is the BlendMerge + HAS_CLIP=1 scenario.
  canvas->save();
  canvas->clipRect(Rect::MakeLTRB(10.5f, 10.5f, 189.5f, 189.5f));
  Paint paint;
  paint.setColor(Color::Green());
  paint.setColorFilter(ColorFilter::Blend(Color::Blue(), BlendMode::Multiply));
  canvas->drawRect(Rect::MakeWH(200, 200), paint);
  canvas->restore();
  context->flushAndSubmit(true);
  printf("  [BlendMergeClip] hit=%u miss=%u\n", cache->hitCount(), cache->missCount());
  EXPECT_GT(cache->hitCount(), 0u);
  cache->unload();
}

TGFX_TEST(ShaderPermutationTest, QuadTextureFillShaderRegistry) {
  auto& factories = ShaderRegistry::All();
  bool found = false;
  for (auto& factory : factories) {
    auto shader = factory();
    auto shaderInfo = shader->info();
    if (shaderInfo.name == "QuadTextureFillShader") {
      found = true;
      EXPECT_EQ(shaderInfo.vertDomain.dimensionCount(), 5u);
      EXPECT_EQ(shaderInfo.vertDomain.totalCount(), 32u);
      // FragDims: 8 bools + 1 int(3) = 2^8 * 3 = 768 total permutations
      EXPECT_EQ(shaderInfo.fragDomain.dimensionCount(), 9u);
      EXPECT_EQ(shaderInfo.fragDomain.totalCount(), 768u);
      EXPECT_EQ(shaderInfo.vertexFile, "level1/quad_texture_fill.vert");
      EXPECT_EQ(shaderInfo.fragmentFile, "level1/quad_texture_fill.frag");
    }
  }
  EXPECT_TRUE(found);
}

TGFX_TEST(ShaderPermutationTest, QuadTextureFillShouldCompile) {
  auto& factories = ShaderRegistry::All();
  for (auto& factory : factories) {
    auto shader = factory();
    auto shaderInfo = shader->info();
    if (shaderInfo.name != "QuadTextureFillShader") {
      continue;
    }
    int compiledCount = 0;
    for (uint32_t vi = 0; vi < shaderInfo.vertDomain.totalCount(); vi++) {
      auto vertValues = shaderInfo.vertDomain.decode(vi);
      for (uint32_t fi = 0; fi < shaderInfo.fragDomain.totalCount(); fi++) {
        auto fragValues = shaderInfo.fragDomain.decode(fi);
        if (shaderInfo.shouldCompile(vi, fi, vertValues, fragValues)) {
          compiledCount++;
        }
      }
    }
    // After adding HAS_CLAMP_SUBSET dimension: 432 valid vert×frag pairs survive ShouldCompile.
    EXPECT_EQ(compiledCount, 432);
  }
}

TGFX_TEST(ShaderPermutationTest, EffectDecomposerTripleFP) {
  auto image = MakeImage("resources/apitest/test_timestretch.png");
  ASSERT_TRUE(image != nullptr);
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 200, 200, false, 1, false, 0, ColorSpace::DisplayP3());
  ASSERT_TRUE(surface != nullptr);
  Paint paint;
  paint.setColorFilter(ColorFilter::Luma());
  surface->getCanvas()->drawImage(image, 0.f, 0.f, &paint);
  context->flushAndSubmit(true);
  Bitmap bitmap;
  bitmap.allocPixels(200, 200);
  auto* pixelData = bitmap.lockPixels();
  ASSERT_TRUE(pixelData != nullptr);
  ASSERT_TRUE(surface->readPixels(bitmap.info(), pixelData));
  bitmap.unlockPixels();
  auto* bytes = reinterpret_cast<const uint8_t*>(bitmap.lockPixels());
  bool hasNonZero = false;
  for (int i = 0; i < 200 * 200 * 4; i++) {
    if (bytes[i] != 0) {
      hasNonZero = true;
      break;
    }
  }
  bitmap.unlockPixels();
  EXPECT_TRUE(hasNonZero);
}

}  // namespace tgfx

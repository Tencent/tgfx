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

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <set>
#include <vector>
#include "base/TGFXTest.h"
#include "core/filters/GaussianBlurImageFilter.h"
#include "core/utils/BlockAllocator.h"
#include "gpu/ContextInternal.h"
#include "gpu/GlobalCache.h"
#include "gpu/ProgramInfo.h"
#include "gpu/ProgramWriter.h"
#include "gpu/ProxyProvider.h"
#include "gpu/aot/bind/Binder.h"
#include "gpu/aot/bundle/EmbeddedShaderBundles.h"
#include "gpu/aot/bundle/PrecompiledShaderCache.h"
#include "gpu/aot/chain/AOTChainBuilder.h"
#include "gpu/aot/ir/DrawIR.h"
#include "gpu/aot/ir/Lowering.h"
#include "gpu/aot/plan/AOTEffectDecomposer.h"
#include "gpu/aot/select/FamilyMatch.h"
#include "gpu/aot/select/ProgramSelection.h"
#include "gpu/aot/select/ShaderMatchHelpers.h"
#include "gpu/aot/select/ShaderProfileResolver.h"
#include "gpu/processors/ClampedGradientEffect.h"
#include "gpu/processors/ConstColorProcessor.h"
#include "gpu/processors/DeviceSpaceTextureEffect.h"
#include "gpu/processors/RRectEffect.h"
#include "gpu/processors/RadialGradientLayout.h"
#include "gpu/processors/RectEffect.h"
#include "gpu/processors/SingleIntervalGradientColorizer.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/processors/TiledTextureEffect.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "gpu/shaders/Families.h"
#include "gpu/shaders/PrecompiledShader.h"
#include "gpu/shaders/ShaderPermutation.h"
#include "gpu/shaders/contract/ShaderBundleContract.h"
#include "gtest/gtest.h"
#include "tgfx/core/Bitmap.h"
#include "tgfx/core/ColorFilter.h"
#include "tgfx/core/ColorSpace.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Pixmap.h"
#include "tgfx/core/Shader.h"
#include "tgfx/core/Surface.h"
#include "utils/AOTToleranceCompare.h"
#include "utils/ProjectPath.h"
#include "utils/ReachableSet.h"
#include "utils/TestUtils.h"
#include "zstd.h"

namespace tgfx {

#ifndef TGFX_BACKEND_NAME
#define TGFX_BACKEND_NAME "opengl"
#endif

// The bundle tag for this build: the backend-name prefix, except that the SwiftShader build runs
// an ES context and consumes the opengles bundle instead of the desktop one.
static std::string BundleTag() {
  std::string backend = TGFX_BACKEND_NAME;
  auto pos = backend.find('-');
  if (pos != std::string::npos) {
    backend = backend.substr(0, pos);
  }
#if defined(TGFX_USE_SWIFTSHADER)
  if (backend == "opengl") {
    backend = "opengles";
  }
#endif
  return backend;
}

// The backend a standalone PrecompiledShaderCache must be constructed with so bundle loads
// pass the profile-tag check outside of a Context.
static Backend TestBackend() {
  std::string backend = TGFX_BACKEND_NAME;
  auto pos = backend.find('-');
  if (pos != std::string::npos) {
    backend = backend.substr(0, pos);
  }
  if (backend == "opengl") {
    return Backend::OpenGL;
  }
  if (backend == "metal") {
    return Backend::Metal;
  }
  if (backend == "vulkan") {
    return Backend::Vulkan;
  }
  if (backend == "webgpu") {
    return Backend::WebGPU;
  }
  if (backend == "d3d12") {
    return Backend::D3D12;
  }
  return Backend::Unknown;
}

static std::shared_ptr<Image> MakeTexture2DImage(Context* context,
                                                 const std::shared_ptr<Image>& image) {
  auto surface = Surface::Make(context, image->width(), image->height(), false, 1, true);
  if (surface == nullptr) {
    return nullptr;
  }
  surface->getCanvas()->drawImage(image, 0, 0);
  auto snapshot = surface->makeImageSnapshot();
  context->flushAndSubmit(true);
  return snapshot;
}

static GeometryLayout MakeSingleIntervalGeometry(int gpType, bool hasVertexCoverage) {
  auto aa = hasVertexCoverage ? AAType::Coverage : AAType::None;
  if (gpType == 0) {
    return DefaultGeometryLayout(PMColor::White(), aa, Matrix::I(), Matrix::I());
  }
  return QuadGeometryLayoutOf(8, 8, aa, PMColor::White(), Matrix::I(), false);
}

static std::optional<DstTextureInfo> MakeSingleIntervalDst(Context* context, int xpType) {
  if (xpType == 0) {
    return std::nullopt;
  }
  DstTextureInfo dstTextureInfo = {};
  if (xpType == 1) {
    auto proxy = context->proxyProvider()->createTextureProxy({}, 8, 8, PixelFormat::RGBA_8888);
    if (proxy == nullptr || proxy->getTextureView() == nullptr) {
      ADD_FAILURE() << "the dst texture proxy could not be created";
      return std::nullopt;
    }
    dstTextureInfo = {std::move(proxy), {}};
  }
  // A present value with an empty proxy is the framebuffer-fetch state (xpType == 2).
  return dstTextureInfo;
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

TGFX_TEST(ShaderPermutationTest, TypedVertDimsDeclaresTheSameDomain) {
  // A declared family's domain is built from the module declarations; the RoundStrokeRect family
  // carries the geometry's UV_MATRIX radix (2) and the Xfer HAS_XP radix (3), and the flattened
  // ordinal is UV_MATRIX + 2 * HAS_XP.
  const auto* family = FindShaderInfo("RoundStrokeRect");
  ASSERT_NE(family, nullptr);
  const auto& domain = family->vertDomain;
  EXPECT_EQ(domain.dimensionCount(), 2u);
  EXPECT_EQ(domain.totalCount(), 6u);
  EXPECT_EQ(domain.defineListFor(1)[0], "RoundStrokeRectGeometry_UV_MATRIX=1");
}

TGFX_TEST(ShaderPermutationTest, ShaderRegistry) {
  // Every family is a declaration row now: the retired shells are neither registered nor
  // enumerable.
  for (const char* retired :
       {"TextureFillShader", "TextureColorMatrixShader", "DeviceSpaceTextureShader",
        "PointwiseDirectShader", "PointwiseChainShader", "YUVTextureFillShader"}) {
    EXPECT_EQ(FindShaderInfo(retired), nullptr) << retired;
    EXPECT_FALSE(EnumerateReachablePermutations(retired).has_value()) << retired;
  }
  const auto* maskFill = FindShaderInfo("MaskFill");
  ASSERT_NE(maskFill, nullptr);
  // The declaration-driven families encode HAS_XP(int3) = 3 permutations; the one domain drives
  // both stages (the vertex text just never reads the dimension).
  EXPECT_EQ(maskFill->vertDomain.totalCount(), 3u);
  EXPECT_EQ(maskFill->vertDomain.dimensionCount(), 1u);
  EXPECT_EQ(maskFill->fragDomain.totalCount(), 3u);
  EXPECT_EQ(maskFill->fragDomain.dimensionCount(), 1u);
}

TGFX_TEST(ShaderPermutationTest, ShaderRegistryEnumerators) {
  std::set<std::string> names;
  for (size_t index = 0; index < FamilyCount(); ++index) {
    const auto info = AssembledFamilyInfo(Families()[index]);
    SCOPED_TRACE(info.name);
    EXPECT_TRUE(names.insert(info.name).second);
    const auto* registered = FindShaderInfo(info.name);
    ASSERT_NE(registered, nullptr);
    EXPECT_EQ(registered->textureReads, info.textureReads);
    auto reachable = EnumerateReachablePermutations(info.name);
    ASSERT_TRUE(reachable.has_value());
    ASSERT_FALSE(reachable->empty());
    for (const auto& indices : *reachable) {
      EXPECT_TRUE(info.IsBuildable(indices.first, indices.second));
    }
    EXPECT_EQ(FindShaderInfo(info.name), registered);
  }
  EXPECT_FALSE(EnumerateReachablePermutations("PointwiseChainShader").has_value());
  // The declaration-driven families enumerate their own ordinals: the geometry IS the family, so
  // SolidColor/MaskFill list HAS_XP alone and TiledTextureFill leads with the geometry radix.
  const std::set<std::pair<uint32_t, uint32_t>> solidColorPairs = {{0, 0}, {1, 1}, {2, 2}};
  EXPECT_EQ(*EnumerateReachablePermutations("SolidColor"), solidColorPairs);
  EXPECT_EQ(*EnumerateReachablePermutations("MaskFill"), solidColorPairs);
  const std::set<std::pair<uint32_t, uint32_t>> tiledFillPairs = {{0, 0}, {1, 1}, {2, 2},
                                                                  {3, 3}, {4, 4}, {5, 5}};
  EXPECT_EQ(*EnumerateReachablePermutations("TiledTextureFill"), tiledFillPairs);
  // The ShapeInstancedTextureCoverage family keeps 2 pairs: the gradient radix alone, with the
  // geometry's VERTEX_COLOR and the Xfer's HAS_XP pinned to 0 by the product rule.
  const std::set<std::pair<uint32_t, uint32_t>> shapeCoveragePairs = {{0, 0}, {2, 2}};
  EXPECT_EQ(*EnumerateReachablePermutations("ShapeInstancedTextureCoverage"), shapeCoveragePairs);
  // The RRect family (the declaration-driven successor of NonAARRectFillShader) keeps 6 pairs: the
  // [PRESENT, HAS_XP] product. The stroke form is the runtime StrokeEnabled uniform, so it is no
  // dimension; the match rule still rejects a textured stroke draw at runtime.
  const auto rrectPairs = EnumerateReachablePermutations("RRect");
  ASSERT_TRUE(rrectPairs.has_value());
  EXPECT_EQ(rrectPairs->size(), 6u);
  EXPECT_EQ(FindShaderInfo("UnknownShader"), nullptr);
  EXPECT_FALSE(EnumerateReachablePermutations("UnknownShader").has_value());
  EXPECT_FALSE(EnumerateReachablePermutations("UnknownShader").has_value());
}

// Reads a GLSL source file with its #include lines recursively expanded (bounded), so a check can
// match text that lives in an .inc module. Includes resolve against the shader root, matching the
// build tool. Returns an empty string when the file cannot be opened.

// Returns the offset of the first whole-word occurrence of identifier in text, or npos. A bare
// substring match would count "HAS_XP" as referenced inside "HAS_XPP" and miss real typos.

TGFX_TEST(ShaderPermutationTest, DimensionDefinesAppearInGLSL) {
  // Retired with the handwritten family sources: every family's stage text is assembled from its
  // declarations, so a dimension's define coverage is checked against the generated text by the
  // build (the assembler emits the conditions from the same declarations).
}
// Death tests need fork/exec, which Emscripten does not provide; GTEST_HAS_DEATH_TEST is 0 for
// wasm builds, where these compile out.
// The three rejections below die on assert(), which NDEBUG removes: a release build has nothing
// to terminate on, so the tests only describe a debug-build contract.
#if GTEST_HAS_DEATH_TEST && DEBUG
#endif
// MatchProgram runs without a failure sink (a plan pass has no diagnostic recording), so a
// rejection must be a plain miss instead of dereferencing the null sink.
TGFX_TEST(ShaderPermutationTest, ProgramMatchRejectsOverBudgetWithoutFailureSink) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto renderTargetProxy = RenderTargetProxy::Make(context, 8, 8, false);
  ASSERT_NE(renderTargetProxy, nullptr);
  auto renderTarget = renderTargetProxy->getRenderTarget();
  ASSERT_NE(renderTarget, nullptr);
  BlockAllocator allocator;
  auto gp = MakeSingleIntervalGeometry(0, false);
  AOTProgram program = {};
  AOTChainSlot gradientSlot = {};
  gradientSlot.op = AOTChainOp::Gradient;
  program.slots.push_back(gradientSlot);
  program.slots.push_back(gradientSlot);
  ProgramInfo programInfo(renderTarget.get(), gp, std::nullopt, BlendMode::SrcOver, false);
  // Two gradient slots exceed the one-gradient budget: the shape has no declared variant.
  EXPECT_FALSE(MatchChainVariant(program, &programInfo).has_value());
}

TGFX_TEST(ShaderPermutationTest, LUTGradientWithRealMaskUsesAllLeafSelectors) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto* cache = ContextCacheAccess::Cache(context);
  ASSERT_TRUE(LoadEmbeddedBundle(cache, context->backend()));
  auto renderTargetProxy = RenderTargetProxy::Make(context, 8, 8, false);
  ASSERT_NE(renderTargetProxy, nullptr);
  auto renderTarget = renderTargetProxy->getRenderTarget();
  ASSERT_NE(renderTarget, nullptr);
  for (int lutLeaf = 0; lutLeaf < 4; ++lutLeaf) {
    SCOPED_TRACE(lutLeaf);
    BlockAllocator allocator;
    std::vector<std::shared_ptr<TextureProxy>> leafProxies;
    std::vector<AOTChainSlot> slots(static_cast<size_t>(lutLeaf) + 1);
    for (int index = 0; index < lutLeaf; ++index) {
      auto proxy = context->proxyProvider()->createTextureProxy({}, 8, 8, PixelFormat::RGBA_8888);
      ASSERT_NE(proxy, nullptr);
      leafProxies.push_back(proxy);
      slots[static_cast<size_t>(index)].op = AOTChainOp::Texture;
      slots[static_cast<size_t>(index)].outRegister = index;
    }
    auto lutProxy = context->proxyProvider()->createTextureProxy({}, 8, 1, PixelFormat::RGBA_8888);
    ASSERT_NE(lutProxy, nullptr);
    auto maskProxy = context->proxyProvider()->createTextureProxy({}, 8, 8, PixelFormat::ALPHA_8);
    ASSERT_NE(maskProxy, nullptr);
    auto& gradient = slots.back();
    gradient.op = AOTChainOp::Gradient;
    gradient.in0 = -1;
    gradient.outRegister = lutLeaf;
    gradient.gradient.colorizerKind = 3;
    gradient.gradient.lutProxy = lutProxy;
    gradient.gradient.coordMatrix = Matrix::MakeTrans(4, 6);
    AOTProgram program = {};
    program.slots = slots;
    program.rootRegister = static_cast<size_t>(lutLeaf);
    program.lutLeafIndex = lutLeaf;
    program.hasMaskTexture = true;
    for (const auto& proxy : leafProxies) {
      program.bindings.push_back({AOTBinding::Kind::Leaf, proxy, {}, Matrix::I(), {}});
    }
    program.bindings.push_back({AOTBinding::Kind::Lut, lutProxy, {}, Matrix::I(), {}});
    while (program.bindings.size() < 4) {
      program.bindings.push_back({AOTBinding::Kind::Padding, lutProxy, {}, Matrix::I(), {}});
    }
    program.bindings.push_back({AOTBinding::Kind::Mask, maskProxy, {}, Matrix::I(), {}});
    program.samplerChildCount = 4;
    // The admission the compile runs on the same data: an Automatic profile resolves onto the
    // gradient-carrying extended kind, and the slot rules accept the layout.
    AOTResourceProfile profile = AOTResourceProfile::Automatic;
    std::vector<int> tiledLeafIndices = {};
    ASSERT_TRUE(AOTChainBuilder::ValidateSlots(
        program.slots, program.rootRegister, program.coverageRootRegister,
        program.clipCoverageRegister, leafProxies.size(), true, tiledLeafIndices, &profile));
    program.profile = profile;
    EXPECT_EQ(program.profile, AOTResourceProfile::TiledGraph);
    EXPECT_EQ(program.lutLeafIndex, lutLeaf);
    EXPECT_TRUE(program.hasMaskTexture);
    EXPECT_EQ(program.samplerChildCount, 4u);
    auto transforms = BuildChainCoordTransforms(program);
    // The chain-wide gradient matrix first, then one per sampler-binding child (the real device
    // mask contributes none).
    ASSERT_EQ(transforms.size(), 1u + program.bindings.size() - (program.hasMaskTexture ? 1u : 0u));
    EXPECT_EQ(transforms[0].matrix, gradient.gradient.coordMatrix);
    EXPECT_EQ(
        program.bindings[static_cast<size_t>(lutLeaf)].textureProxy->getTextureView()->getTexture(),
        lutProxy->getTextureView()->getTexture());
    auto gp = MakeSingleIntervalGeometry(1, true);
    ProgramInfo programInfo(renderTarget.get(), gp, std::nullopt, BlendMode::SrcOver, false);
    // The interpreter's own match: a program-holding draw resolves through the variant
    // derivation, not the family walk (whose dedicated families claim the empty processor list).
    auto match = MatchChainVariant(program, &programInfo);
    ASSERT_TRUE(match.has_value());
    // The Chain body's module slot carries PROFILE: 5 is the gradient kind with the four leaf
    // slots (the legacy TEXTURE_COUNT=1 / RESOURCE_KIND=1 cell).
    constexpr uint8_t ChainBodySlot = 2;
    EXPECT_EQ(DimValueOf(Families()[match->family], *match, ChainBodySlot, 0), 5);
  }
}

TGFX_TEST(ShaderPermutationTest, QuadTextureLocalMaskServesBothCoveragePolarities) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  if (context->backend() == Backend::OpenGL) {
    GTEST_SKIP() << "QuadTextureFill is outside the OpenGL stage 1 whitelist";
  }
  auto renderTargetProxy = RenderTargetProxy::Make(context, 8, 8, false);
  ASSERT_NE(renderTargetProxy, nullptr);
  auto renderTarget = renderTargetProxy->getRenderTarget();
  ASSERT_NE(renderTarget, nullptr);

  for (bool inverted : {false, true}) {
    BlockAllocator allocator;
    auto gp = QuadGeometryLayoutOf(8, 8, AAType::None, PMColor::White(), Matrix::I(), false);
    auto colorProxy =
        context->proxyProvider()->createTextureProxy({}, 8, 8, PixelFormat::RGBA_8888);
    auto maskProxy = context->proxyProvider()->createTextureProxy({}, 8, 8, PixelFormat::RGBA_8888);
    auto color = TextureEffect::Make(&allocator, std::move(colorProxy));
    auto mask = TextureEffect::Make(&allocator, std::move(maskProxy));
    auto coverage = FragmentProcessor::MulInputByChildAlpha(&allocator, std::move(mask), inverted);
    ASSERT_NE(color, nullptr);
    ASSERT_NE(coverage, nullptr);
    ProgramInfo programInfo(renderTarget.get(), gp, std::nullopt, BlendMode::SrcOver, false);
    // The matcher reads the draw's lowering graph through the carried IR; a hand-built ProgramInfo
    // carries none, and the families reject an IR-less shape outright. Lower the same processors
    // the way StandardDrawOp would and attach the result before matching.
    auto lowered = LowerDraw({color.get()}, {coverage.get()}, BlendMode::SrcOver, &allocator);
    ASSERT_TRUE(lowered.has_value());
    programInfo.setCarriedIR(&*lowered);
    auto match = SelectProgram(context, programInfo, SelectionScope::AnyFamily);
    // Both polarities are servable: an inverted local mask lowers to an SrcOut xfer node the IR
    // expresses, so the walk claims the draw through the chain family (the xfer operator rides
    // the pointwise table) instead of the dedicated local-mask kernel. The pre-IR matcher
    // rejected the inverted form because its tree walk could not see the operator, not because
    // the shader cannot evaluate it; what matters is that neither polarity is dropped.
    ASSERT_TRUE(match.has_value()) << (inverted ? "inverted" : "straight");
    if (inverted) {
      EXPECT_EQ(Families()[match->variant.family].name, "Chain");
    } else {
      EXPECT_EQ(Families()[match->variant.family].name, "QuadTexture");
    }
  }
}

static void ExpectBundleEntryCounts(const PrecompiledShaderCache& cache,
                                    const std::string& profileTag) {
  auto profile = ParseProfileTag(profileTag);
  ASSERT_TRUE(profile.has_value()) << profileTag;
  size_t vertexCount = 0;
  size_t fragmentCount = 0;
  // Every family is a row of the declaration table.
  std::set<std::string> names;
  for (size_t index = 0; index < FamilyCount(); ++index) {
    names.insert(Families()[index].name);
  }
  for (const auto& name : names) {
    const auto* info = FindShaderInfo(name);
    ASSERT_NE(info, nullptr) << name;
    auto reachable = EnumerateReachablePermutations(info->name);
    ASSERT_TRUE(reachable.has_value()) << info->name;
    std::set<uint32_t> vertexIndices;
    std::set<uint32_t> fragmentIndices;
    for (const auto& permutation : *reachable) {
      if (!PermutationCompilesForBackend(*info, permutation.first, permutation.second, *profile)) {
        continue;
      }
      vertexIndices.insert(permutation.first);
      fragmentIndices.insert(permutation.second);
    }
    vertexCount += vertexIndices.size();
    fragmentCount += fragmentIndices.size();
  }
  EXPECT_EQ(cache.vertexEntryCount(), vertexCount);
  EXPECT_EQ(cache.fragmentEntryCount(), fragmentCount);
}

TGFX_TEST(ShaderPermutationTest, PrecompiledBundleLoad) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto* cache = ContextCacheAccess::Cache(context);
  ASSERT_TRUE(LoadEmbeddedBundle(cache, context->backend()));
  std::string expectedTag = BundleTag();
  ExpectBundleEntryCounts(*cache, expectedTag);
  EXPECT_EQ(cache->profileTag(), expectedTag);
}

TGFX_TEST(ShaderPermutationTest, ShaderCacheStats) {
  PrecompiledShaderCache cache(nullptr, TestBackend());

  EXPECT_EQ(cache.hitCount(), 0u);
  EXPECT_EQ(cache.missCount(), 0u);
  EXPECT_EQ(cache.aotStageCount(PrecompiledAOTStage::Attempt), 0u);

  cache.setDiagnosticRecordingEnabled(true);
  cache.recordAOTStage(PrecompiledAOTStage::Attempt);
  cache.recordAOTStage(PrecompiledAOTStage::CacheAvailable);
  cache.recordAOTStage(PrecompiledAOTStage::PermutationMatched);
  cache.recordArtifactHit();
  cache.recordAOTStage(PrecompiledAOTStage::VertexModuleCreated);
  cache.recordAOTStage(PrecompiledAOTStage::FragmentModuleCreated);
  PrecompiledHitRecord hitRecord;
  hitRecord.shaderName = "TestShader";
  hitRecord.pipelineSignature = "GP=Test;ColorFP=[];CoverageFP=[];XP=Test";
  hitRecord.variantId = 3;
  cache.recordAOTStage(PrecompiledAOTStage::PipelineCreated, hitRecord);

  cache.recordAOTStage(PrecompiledAOTStage::Attempt);
  cache.recordAOTStage(PrecompiledAOTStage::CacheAvailable);
  PrecompiledFallbackRecord fallbackRecord;
  fallbackRecord.pipelineSignature = "GP=Test;ColorFP=[];CoverageFP=[];XP=Test";
  cache.recordArtifactMiss(PrecompiledFallbackReason::NoMatchingRule, fallbackRecord);

  EXPECT_EQ(cache.hitCount(), 1u);
  EXPECT_EQ(cache.missCount(), 1u);
  EXPECT_EQ(cache.aotStageCount(PrecompiledAOTStage::Attempt), 2u);
  EXPECT_EQ(cache.aotStageCount(PrecompiledAOTStage::CacheAvailable), 2u);
  EXPECT_EQ(cache.aotStageCount(PrecompiledAOTStage::PermutationMatched), 1u);
  EXPECT_EQ(cache.aotStageCount(PrecompiledAOTStage::ArtifactsFound), 1u);
  EXPECT_EQ(cache.aotStageCount(PrecompiledAOTStage::VertexModuleCreated), 1u);
  EXPECT_EQ(cache.aotStageCount(PrecompiledAOTStage::FragmentModuleCreated), 1u);
  EXPECT_EQ(cache.aotStageCount(PrecompiledAOTStage::PipelineCreated), 1u);
  EXPECT_EQ(cache.fallbackCount(PrecompiledFallbackReason::NoMatchingRule), 1u);
  auto hitRecords = cache.hitRecords();
  ASSERT_EQ(hitRecords.size(), 1u);
  EXPECT_EQ(hitRecords[0].shaderName, hitRecord.shaderName);
  EXPECT_EQ(hitRecords[0].pipelineSignature, hitRecord.pipelineSignature);
  auto fallbackRecords = cache.fallbackRecords();
  ASSERT_EQ(fallbackRecords.size(), 1u);
  EXPECT_EQ(fallbackRecords[0].reason, PrecompiledFallbackReason::NoMatchingRule);

  EXPECT_EQ(cache.aotStageCount(PrecompiledAOTStage::PipelineCreated), 1u);
  EXPECT_EQ(cache.hitRecords().size(), 1u);
  EXPECT_EQ(cache.fallbackRecords().size(), 1u);

  cache.resetStats();
  EXPECT_EQ(cache.hitCount(), 0u);
  EXPECT_EQ(cache.missCount(), 0u);
  EXPECT_EQ(cache.aotStageCount(PrecompiledAOTStage::Attempt), 0u);
  EXPECT_EQ(cache.fallbackCount(PrecompiledFallbackReason::NoMatchingRule), 0u);
  EXPECT_TRUE(cache.hitRecords().empty());
  EXPECT_TRUE(cache.fallbackRecords().empty());

  cache.recordArtifactMiss(PrecompiledFallbackReason::VertexArtifactMissing);
  cache.recordFailure(PrecompiledFallbackReason::VertexModuleCreationFailed);
  cache.recordFailure(PrecompiledFallbackReason::PipelineCreationFailed);
  cache.recordArtifactHit();
  EXPECT_EQ(cache.hitCount(), 1u);
  EXPECT_EQ(cache.missCount(), 1u);
  EXPECT_EQ(cache.fallbackCount(PrecompiledFallbackReason::VertexArtifactMissing), 1u);
  EXPECT_EQ(cache.fallbackCount(PrecompiledFallbackReason::VertexModuleCreationFailed), 1u);
  EXPECT_EQ(cache.fallbackCount(PrecompiledFallbackReason::PipelineCreationFailed), 1u);
}

TGFX_TEST(ShaderPermutationTest, EmbeddedBundleLoadFromMemory) {
  // Verify that PrecompiledShaderCache can load a bundle from in-memory data (the same interface
  // used by the embedded bundle mechanism in Context initialization).
  auto data = CopyEmbeddedBundle(TestBackend());
  if (data.empty()) {
    GTEST_SKIP() << "This build embeds no bundle for the backend, skipping embedded load test";
    return;
  }

  // Load from memory (simulates embedded bundle)
  PrecompiledShaderCache cache(nullptr, TestBackend());
  ASSERT_TRUE(cache.loadBundle(data.data(), data.size()));
  EXPECT_TRUE(cache.isLoaded());
  EXPECT_GT(cache.vertexEntryCount(), 0u);
  EXPECT_GT(cache.fragmentEntryCount(), 0u);
  std::string expectedTag2 = BundleTag();
  EXPECT_EQ(cache.profileTag(), expectedTag2);
}

TGFX_TEST(ShaderPermutationTest, EmbeddedBundleInvalidData) {
  // Verify that loadBundle rejects invalid data gracefully.
  PrecompiledShaderCache cache(nullptr, TestBackend());

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

// Re-loads the embedded bundle after a test that swaps bundle state, so the thread-local Context
// the following tests share keeps the production state.
class ScopedBundleStateRestore {
 public:
  ScopedBundleStateRestore(PrecompiledShaderCache* cache, Backend backend)
      : cache(cache), backend(backend), wasLoaded(cache->isLoaded()) {
  }

  ~ScopedBundleStateRestore() {
    if (wasLoaded && !cache->isLoaded()) {
      LoadEmbeddedBundle(cache, backend);
    }
  }

  ScopedBundleStateRestore(const ScopedBundleStateRestore&) = delete;
  ScopedBundleStateRestore& operator=(const ScopedBundleStateRestore&) = delete;

 private:
  PrecompiledShaderCache* cache = nullptr;
  Backend backend = Backend::Unknown;
  bool wasLoaded = false;
};

// The stronger sibling of ScopedBundleStateRestore: a test that REPLACES the loaded bundle (a fake
// one loaded to provoke a miss) leaves isLoaded() true, so the "restore when unloaded" guard above
// does not fire. This one puts the embedded bundle back no matter what the test left in the cache,
// because the thread-local Context the next test locks shares that cache: without the reload,
// every later draw of the same suite runs against the fake bundle.
class ScopedEmbeddedBundleReload {
 public:
  ScopedEmbeddedBundleReload(PrecompiledShaderCache* cache, Backend backend)
      : cache(cache), backend(backend) {
  }

  ~ScopedEmbeddedBundleReload() {
    LoadEmbeddedBundle(cache, backend);
  }

  ScopedEmbeddedBundleReload(const ScopedEmbeddedBundleReload&) = delete;
  ScopedEmbeddedBundleReload& operator=(const ScopedEmbeddedBundleReload&) = delete;

 private:
  PrecompiledShaderCache* cache = nullptr;
  Backend backend = Backend::Unknown;
};

static std::string HexU32(uint32_t value) {
  char buffer[16] = {};
  std::snprintf(buffer, sizeof(buffer), "0x%08x", value);
  return buffer;
}

// Guards the bundle embedded in the build, the one every Context loads and every AOT test
// consumes. When it is missing or built for another toolchain ABI, the draws and tests that
// depend on it fail far from the cause (a null pixel read, a cache that never loads). This test
// fails first and says which of those it is.
TGFX_TEST(ShaderPermutationTest, EmbeddedBundleIsPresentAndCurrent) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto backend = context->backend();
  if (backend == Backend::Unknown) {
    GTEST_SKIP() << "This backend has no precompiled shader bundle";
  }
  const std::string tag = BundleTag();

  auto [data, size] = GetEmbeddedBundle(backend);
  ASSERT_TRUE(data != nullptr && size > 0)
      << "No \"" << tag
      << "\" bundle is embedded in this build. Either TGFX_EMBED_SHADER_BUNDLES is OFF or the "
         "bundle generation step did not produce one; every AOT test fails without it.";
  ASSERT_GE(size, BundleFormat::HeaderSize)
      << "The embedded \"" << tag << "\" bundle is smaller than its header";

  // The ABI is read from the raw header first so a mismatch reports both numbers, instead of the
  // loader's log line that the test output usually buries.
  auto embeddedABI = TestReadU32LE(data + 16);
  ASSERT_EQ(embeddedABI, ExpectedToolchainABI)
      << "The embedded \"" << tag << "\" bundle records toolchain ABI " << HexU32(embeddedABI)
      << " but the runtime requires " << HexU32(ExpectedToolchainABI)
      << ". The embedded bundle is stale: the bundle generation step did not rerun after the ABI "
         "changed, and Context silently refuses to load it.";

  // Load it through the Context's own cache, which applies the full acceptance contract (format,
  // ABI, profile tag of this context, content identity hash).
  auto* cache = ContextCacheAccess::Cache(context);
  ScopedBundleStateRestore restore(cache, backend);
  ASSERT_TRUE(LoadEmbeddedBundle(cache, backend))
      << "The cache rejected the embedded \"" << tag << "\" bundle; see the PrecompiledShaderCache "
      << "log lines above for the reason";
  EXPECT_TRUE(cache->isLoaded());
  EXPECT_EQ(cache->bundleToolchainABI(), ExpectedToolchainABI);
  EXPECT_EQ(cache->profileTag(), tag);
  EXPECT_NE(cache->bundleIdentityHash(), 0u)
      << "A zero identity hash marks a legacy bundle without a content check";
  EXPECT_GT(cache->vertexEntryCount(), 0u);
  EXPECT_GT(cache->fragmentEntryCount(), 0u);
}

// The Context records what became of the embedded bundle, so a run that never loaded it can say
// whether that was by design or a defect.
TGFX_TEST(ShaderPermutationTest, ContextRecordsTheEmbeddedBundleState) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto backend = context->backend();
  if (backend == Backend::Unknown) {
    GTEST_SKIP() << "This backend has no precompiled shader bundle";
  }
  auto state = ContextCacheAccess::Cache(context)->embeddedBundleState();
  auto [data, size] = GetEmbeddedBundle(backend);
  if (data == nullptr || size == 0) {
    EXPECT_EQ(state, EmbeddedBundleState::NoBundleForBackend)
        << "A build that embeds no bundle for the backend must say so, not look rejected";
    return;
  }
  EXPECT_EQ(state, EmbeddedBundleState::Loaded)
      << "A build whose embedded bundle is present and current must end in Loaded; "
         "EmbeddedBundleIsPresentAndCurrent says which of the two it is when this fails";
}

// Puts the cache and the recorded bundle state back after a test that changes them.
class ScopedEmbeddedStateRestore {
 public:
  ScopedEmbeddedStateRestore(PrecompiledShaderCache* cache, Backend backend)
      : cache(cache), backend(backend), state(cache->embeddedBundleState()),
        wasLoaded(cache->isLoaded()) {
  }

  ~ScopedEmbeddedStateRestore() {
    cache->setEmbeddedBundleState(state);
    if (wasLoaded && !cache->isLoaded()) {
      LoadEmbeddedBundle(cache, backend);
    }
    cache->resetStats();
  }

  ScopedEmbeddedStateRestore(const ScopedEmbeddedStateRestore&) = delete;
  ScopedEmbeddedStateRestore& operator=(const ScopedEmbeddedStateRestore&) = delete;

 private:
  PrecompiledShaderCache* cache = nullptr;
  Backend backend = Backend::Unknown;
  EmbeddedBundleState state = EmbeddedBundleState::NotAttempted;
  bool wasLoaded = false;
};

// The same question for a real build that embeds nothing for this backend, with nothing simulated:
// only meaningful there, so it is skipped in every build that does embed a bundle.
TGFX_TEST(ShaderPermutationTest, DrawsInABuildWithoutABundleReportNoBundleForBackend) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto* cache = ContextCacheAccess::Cache(context);
  if (cache->embeddedBundleState() != EmbeddedBundleState::NoBundleForBackend) {
    GTEST_SKIP() << "This build embeds a bundle for the backend (or AOT is disabled)";
  }
  ASSERT_FALSE(cache->isLoaded());
  cache->resetStats();
  auto surface = Surface::Make(context, 64, 64);
  ASSERT_TRUE(surface != nullptr);
  Paint paint = {};
  paint.setColor(Color::FromRGBA(0, 128, 255, 255));
  surface->getCanvas()->drawRect(Rect::MakeXYWH(4, 4, 32, 32), paint);
  context->flushAndSubmit(true);
  EXPECT_GT(cache->fallbackCount(PrecompiledFallbackReason::NoBundleForBackend), 0u);
  EXPECT_EQ(cache->fallbackCount(PrecompiledFallbackReason::BundleRejected), 0u);
  EXPECT_EQ(cache->fallbackCount(PrecompiledFallbackReason::CacheNotLoaded), 0u);
}

TGFX_TEST(ShaderPermutationTest, FallbackReasonNamesAreDistinct) {
  std::set<std::string> names;
  for (size_t i = 0; i < static_cast<size_t>(PrecompiledFallbackReason::Count); ++i) {
    names.insert(PrecompiledFallbackReasonName(static_cast<PrecompiledFallbackReason>(i)));
  }
  EXPECT_EQ(names.size(), static_cast<size_t>(PrecompiledFallbackReason::Count));
  EXPECT_EQ(names.count("Unknown"), 0u);
  EXPECT_STREQ(PrecompiledFallbackReasonName(PrecompiledFallbackReason::NoBundleForBackend),
               "NoBundleForBackend");
  EXPECT_STREQ(PrecompiledFallbackReasonName(PrecompiledFallbackReason::BundleRejected),
               "BundleRejected");
}

static void SetTestBundleIdentity(std::vector<uint8_t>* bundle);

static std::vector<uint8_t> MakeTestBundle(const std::string& profileTag, uint32_t vertCount,
                                           uint32_t fragCount, uint32_t hashSeed) {
  constexpr uint32_t HeaderSize = BundleFormat::HeaderSize;
  constexpr uint32_t PoolEntrySize = BundleFormat::PoolEntrySize;
  auto fragPoolOffset = HeaderSize + vertCount * PoolEntrySize;
  auto dataOffset = fragPoolOffset + fragCount * PoolEntrySize;
  auto dataSize = vertCount + fragCount;
  std::vector<uint8_t> bundle(dataOffset + dataSize, 0);
  TestWriteU32LE(bundle.data(), BundleFormat::Magic);
  TestWriteU16LE(bundle.data() + 4, BundleFormat::CurrentVersion);
  TestWriteU32LE(bundle.data() + 16, ExpectedToolchainABI);
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
    TestWriteU32LE(bundle.data() + entryOffset + 4, index);
    TestWriteU32LE(bundle.data() + entryOffset + 8, 1);
    bundle[dataOffset + index] = static_cast<uint8_t>(index + 1);
  }
  for (uint32_t index = 0; index < fragCount; ++index) {
    auto entryOffset = fragPoolOffset + index * PoolEntrySize;
    TestWriteU32LE(bundle.data() + entryOffset, hashSeed + 0x100 + index);
    TestWriteU32LE(bundle.data() + entryOffset + 4, vertCount + index);
    TestWriteU32LE(bundle.data() + entryOffset + 8, 1);
    bundle[dataOffset + vertCount + index] = static_cast<uint8_t>(index + 1);
  }
  SetTestBundleIdentity(&bundle);
  return bundle;
}

static std::vector<uint8_t> MakeReflectedTestBundle(uint16_t version) {
  auto bundle = MakeTestBundle("bounds", 1, 1, 50);
  TestWriteU16LE(bundle.data() + 4, version);
  TestWriteU32LE(bundle.data() + 16, ExpectedToolchainABI);
  TestWriteU32LE(bundle.data() + 44, static_cast<uint32_t>(bundle.size()));
  std::vector<uint8_t> reflection = {
      1, 0, 0, 0, 1, 'u', static_cast<uint8_t>(UniformFormat::Float4)};
  if (version >= 4) {
    reflection.push_back(2);
    reflection.push_back(0);
  }
  if (version >= 5) {
    reflection[2] = 2;  // vertex input count
    // Two inputs in location order with a legal gap at 1, mirroring the shape-instanced kernels
    // that skip the optional slot.
    reflection.insert(reflection.end(), {1, 'a', static_cast<uint8_t>(UniformFormat::Float4), 0});
    reflection.insert(reflection.end(), {1, 'b', static_cast<uint8_t>(UniformFormat::Float2), 3});
  }
  bundle.insert(bundle.end(), reflection.begin(), reflection.end());
  SetTestBundleIdentity(&bundle);
  return bundle;
}

static void SetTestBundleIdentity(std::vector<uint8_t>* bundle) {
  auto data = bundle->data();
  ASSERT_EQ(TestReadU16LE(data + 6), 0u);
  auto vertCount = TestReadU32LE(data + 20);
  auto fragCount = TestReadU32LE(data + 24);
  auto hash = BundleIdentityHashHeader(BundleIdentityHashInit(), TestReadU16LE(data + 4), vertCount,
                                       fragCount, data + 48);
  hash = BundleIdentityHashBytes(hash, data + TestReadU32LE(data + 28),
                                 static_cast<size_t>(vertCount) * BundleFormat::PoolEntrySize);
  hash = BundleIdentityHashBytes(hash, data + TestReadU32LE(data + 32),
                                 static_cast<size_t>(fragCount) * BundleFormat::PoolEntrySize);
  hash = BundleIdentityHashBytes(hash, data + TestReadU32LE(data + 36), TestReadU32LE(data + 40));
  auto reflectionOffset = TestReadU32LE(data + 44);
  if (reflectionOffset != 0) {
    hash =
        BundleIdentityHashBytes(hash, data + reflectionOffset, bundle->size() - reflectionOffset);
  }
  TestWriteU32LE(data + 8, static_cast<uint32_t>(hash));
  TestWriteU32LE(data + 12, static_cast<uint32_t>(hash >> 32));
}

static std::vector<uint8_t> CompressTestBundle(const std::vector<uint8_t>& bundle,
                                               uint16_t compressionType) {
  if (compressionType == 0) {
    return bundle;
  }
  auto dataOffset = TestReadU32LE(bundle.data() + 36);
  auto dataSize = TestReadU32LE(bundle.data() + 40);
  auto reflectionOffset = TestReadU32LE(bundle.data() + 44);
  std::vector<uint8_t> compressedData;
  compressedData.resize(ZSTD_compressBound(dataSize));
  auto compressedSize = ZSTD_compress(compressedData.data(), compressedData.size(),
                                      bundle.data() + dataOffset, dataSize, 1);
  EXPECT_FALSE(ZSTD_isError(compressedSize));
  if (ZSTD_isError(compressedSize)) {
    return {};
  }
  compressedData.resize(compressedSize);
  std::vector<uint8_t> result(bundle.data(), bundle.data() + dataOffset);
  result.insert(result.end(), compressedData.begin(), compressedData.end());
  TestWriteU16LE(result.data() + 6, compressionType);
  if (reflectionOffset != 0) {
    TestWriteU32LE(result.data() + 44, static_cast<uint32_t>(result.size()));
    result.insert(result.end(), bundle.data() + reflectionOffset, bundle.data() + bundle.size());
  }
  return result;
}

static void ExpectBundleRejectedPreservingCache(const std::vector<uint8_t>& invalid) {
  auto original = MakeTestBundle("original", 1, 1, 10);
  PrecompiledShaderCache cache(nullptr, Backend::Unknown);
  ASSERT_TRUE(cache.loadBundle(original.data(), original.size()));
  auto generation = cache.bundleGeneration();
  EXPECT_FALSE(cache.loadBundle(invalid.data(), invalid.size()));
  EXPECT_EQ(cache.bundleGeneration(), generation);
  EXPECT_EQ(cache.profileTag(), "original");
  EXPECT_EQ(cache.vertexEntryCount(), 1u);
  EXPECT_EQ(cache.fragmentEntryCount(), 1u);
  auto vertex = cache.findVertex(10);
  auto fragment = cache.findFragment(10 + 0x100);
  ASSERT_NE(vertex, nullptr);
  ASSERT_NE(fragment, nullptr);
  EXPECT_EQ(vertex->data, (std::vector<uint8_t>{1}));
  EXPECT_EQ(fragment->data, (std::vector<uint8_t>{1}));
  EXPECT_EQ(cache.findVertex(50), nullptr);
  EXPECT_EQ(cache.findFragment(50 + 0x100), nullptr);
}

TGFX_TEST(ShaderPermutationTest, BundleRejectsDisorderedVertexInputLocations) {
  // The vertex input entries must appear in strictly increasing location order: the backends bind
  // attribute slots by location, so a duplicated location leaves one slot unbound and a decreasing
  // one means the serialized order contradicts the declared layout.
  auto duplicate = MakeReflectedTestBundle(5);
  // 'b' location 3 -> 0, duplicating 'a'.
  duplicate.back() = 0;
  ExpectBundleRejectedPreservingCache(duplicate);

  auto decreasing = MakeReflectedTestBundle(5);
  // 'a' location 0 -> 5, so the recorded sequence runs 5, 3.
  auto reflectionOffset = TestReadU32LE(decreasing.data() + 44);
  // Header (4) + 'u' entry (3 + 2 array size) + the 'a' entry's name/format (3).
  decreasing[reflectionOffset + 12] = 5;
  ExpectBundleRejectedPreservingCache(decreasing);
}

TGFX_TEST(ShaderPermutationTest, BundleCompressionAndReflectionRoundTrip) {
  constexpr uint16_t version = BundleFormat::CurrentVersion;
  for (uint16_t compression : {uint16_t{0}, uint16_t{2}}) {
    for (bool withReflection : {false, true}) {
      {
        SCOPED_TRACE(compression);
        SCOPED_TRACE(withReflection);
        auto original =
            withReflection ? MakeReflectedTestBundle(version) : MakeTestBundle("bounds", 1, 1, 50);
        auto bundle = CompressTestBundle(original, compression);
        ASSERT_FALSE(bundle.empty());
        PrecompiledShaderCache cache(nullptr, Backend::Unknown);
        ASSERT_TRUE(cache.loadBundle(bundle.data(), bundle.size()));
        EXPECT_EQ(cache.profileTag(), "bounds");
        EXPECT_EQ(cache.vertexEntryCount(), 1u);
        EXPECT_EQ(cache.fragmentEntryCount(), 1u);
        auto vertex = cache.findVertex(50);
        auto fragment = cache.findFragment(50 + 0x100);
        ASSERT_NE(vertex, nullptr);
        ASSERT_NE(fragment, nullptr);
        for (auto blob : {vertex, fragment}) {
          EXPECT_EQ(blob->data, (std::vector<uint8_t>{1}));
          EXPECT_TRUE(blob->samplers.empty());
          if (withReflection) {
            ASSERT_EQ(blob->uniforms.size(), 1u);
            EXPECT_EQ(blob->uniforms[0].name(), "u");
            EXPECT_EQ(blob->uniforms[0].format(), UniformFormat::Float4);
            EXPECT_EQ(blob->uniforms[0].arraySize(), 2u);
            ASSERT_EQ(blob->vertexInputs.size(), 2u);
            EXPECT_EQ(blob->vertexInputs[0].name, "a");
            EXPECT_EQ(blob->vertexInputs[0].format, UniformFormat::Float4);
            EXPECT_EQ(blob->vertexInputs[0].location, 0u);
            EXPECT_EQ(blob->vertexInputs[1].name, "b");
            EXPECT_EQ(blob->vertexInputs[1].format, UniformFormat::Float2);
            EXPECT_EQ(blob->vertexInputs[1].location, 3u);
          } else {
            EXPECT_TRUE(blob->uniforms.empty());
          }
        }
      }
    }
  }
}

TGFX_TEST(ShaderPermutationTest, BundleRejectsWrappedPoolCountsBeforeHashing) {
  for (uint32_t count : {0x40000000u, 0x80000000u, 0xc0000000u}) {
    for (size_t countOffset : {20u, 24u}) {
      for (uint8_t hashByte : {uint8_t{0}, uint8_t{1}}) {
        auto invalid = MakeTestBundle("bounds", 0, 0, 0);
        invalid[8] = hashByte;
        TestWriteU32LE(invalid.data() + countOffset, count);
        ExpectBundleRejectedPreservingCache(invalid);
      }
    }
  }
}

TGFX_TEST(ShaderPermutationTest, BundleRejectsSectionOverlap) {
  const std::pair<size_t, uint32_t> mutations[] = {{28, 79},
                                                   {32, 80},
                                                   {36, 100},
                                                   {36, 1000},
                                                   {20, std::numeric_limits<uint32_t>::max()},
                                                   {24, std::numeric_limits<uint32_t>::max()},
                                                   {44, 135},
                                                   {44, 1000}};
  for (const auto& mutation : mutations) {
    SCOPED_TRACE(mutation.first);
    SCOPED_TRACE(mutation.second);
    auto invalid = MakeReflectedTestBundle(BundleFormat::CurrentVersion);
    TestWriteU32LE(invalid.data() + mutation.first, mutation.second);
    ExpectBundleRejectedPreservingCache(invalid);
  }
}

TGFX_TEST(ShaderPermutationTest, BundleRejectsBlobOutsideDataPool) {
  for (uint16_t compression : {uint16_t{0}, uint16_t{2}}) {
    auto invalid =
        CompressTestBundle(MakeReflectedTestBundle(BundleFormat::CurrentVersion), compression);
    ASSERT_FALSE(invalid.empty());
    TestWriteU32LE(invalid.data() + BundleFormat::HeaderSize + 16, 2);
    ExpectBundleRejectedPreservingCache(invalid);
    TestWriteU32LE(invalid.data() + BundleFormat::HeaderSize + 16,
                   std::numeric_limits<uint32_t>::max());
    ExpectBundleRejectedPreservingCache(invalid);
  }
}

TGFX_TEST(ShaderPermutationTest, BundleRejectsReflectionOutsidePool) {
  {
    auto invalid = MakeReflectedTestBundle(BundleFormat::CurrentVersion);
    auto reflectionSize =
        static_cast<uint32_t>(invalid.size()) - TestReadU32LE(invalid.data() + 44);
    TestWriteU32LE(invalid.data() + BundleFormat::HeaderSize + 24, reflectionSize);
    ExpectBundleRejectedPreservingCache(invalid);
    TestWriteU32LE(invalid.data() + BundleFormat::HeaderSize + 24,
                   std::numeric_limits<uint32_t>::max());
    ExpectBundleRejectedPreservingCache(invalid);
  }
}

TGFX_TEST(ShaderPermutationTest, BundleRejectsTruncatedReflection) {
  {
    auto original = MakeReflectedTestBundle(BundleFormat::CurrentVersion);
    auto reflectionOffset = TestReadU32LE(original.data() + 44);
    for (size_t size = reflectionOffset; size < original.size(); ++size) {
      SCOPED_TRACE(size);
      auto invalid = original;
      invalid.resize(size);
      ExpectBundleRejectedPreservingCache(invalid);
    }
    original[reflectionOffset + 4] = 255;
    ExpectBundleRejectedPreservingCache(original);
  }
}

TGFX_TEST(ShaderPermutationTest, BundleReflectionContractsWithVerifiedIdentity) {
  constexpr uint16_t version = BundleFormat::CurrentVersion;
  {
    for (uint16_t compression : {uint16_t{0}, uint16_t{2}}) {
      SCOPED_TRACE(version);
      SCOPED_TRACE(compression);
      for (uint8_t format = 0; format <= static_cast<uint8_t>(UniformFormat::Texture2DRectSampler);
           ++format) {
        auto original = MakeReflectedTestBundle(version);
        auto offset = TestReadU32LE(original.data() + 44);
        bool sampler = format >= static_cast<uint8_t>(UniformFormat::Texture2DSampler);
        original[offset] = sampler ? 0 : 1;
        original[offset + 1] = sampler ? 1 : 0;
        original[offset + 6] = format;
        if (sampler) {
          TestWriteU16LE(original.data() + offset + 7, 1);
        }
        SetTestBundleIdentity(&original);
        auto bundle = CompressTestBundle(original, compression);
        PrecompiledShaderCache cache(nullptr, Backend::Unknown);
        ASSERT_TRUE(cache.loadBundle(bundle.data(), bundle.size()));
        auto vertex = cache.findVertex(50);
        ASSERT_NE(vertex, nullptr);
        const auto& entries = sampler ? vertex->samplers : vertex->uniforms;
        ASSERT_EQ(entries.size(), 1u);
        EXPECT_EQ(entries[0].format(), static_cast<UniformFormat>(format));
      }
      for (int mutation = 0; mutation < 8; ++mutation) {
        SCOPED_TRACE(mutation);
        auto invalid = MakeReflectedTestBundle(version);
        auto offset = TestReadU32LE(invalid.data() + 44);
        switch (mutation) {
          case 0:
            invalid[offset + 6] = 255;
            break;
          case 1:
            invalid[offset + 4] = 0;
            break;
          case 2:
            invalid[offset + 5] = 0;
            break;
          case 3:
            invalid[offset + 6] = static_cast<uint8_t>(UniformFormat::Texture2DSampler);
            break;
          case 4:
            invalid[offset] = 0;
            invalid[offset + 1] = 1;
            break;
          case 5: {
            std::vector<uint8_t> duplicate(invalid.begin() + offset + 4, invalid.end());
            invalid.insert(invalid.end(), duplicate.begin(), duplicate.end());
            invalid[offset] = 2;
            break;
          }
          case 6:
            TestWriteU16LE(invalid.data() + offset + 7, 0);
            break;
          case 7:
            invalid[offset] = 0;
            invalid[offset + 1] = 1;
            invalid[offset + 6] = static_cast<uint8_t>(UniformFormat::Texture2DSampler);
            break;
        }
        for (bool withIdentity : {false, true}) {
          if (withIdentity) {
            SetTestBundleIdentity(&invalid);
          }
          ExpectBundleRejectedPreservingCache(CompressTestBundle(invalid, compression));
        }
      }
    }
  }
}

// F04: the recorded toolchain ABI (header offset 16) is the compatibility gate the content
// identity hash cannot provide, because a legacy bundle without a hash (0) carries no
// self-description while its shader names and pool indices may still resolve. A bundle stamped
// with a different ABI — older or newer — must be rejected outright rather than fed to the
// matchers, whose reflection grammar and uniform contracts it predates or postdates.
TGFX_TEST(ShaderPermutationTest, BundleRejectsIncompatibleToolchainABI) {
  // Derived from the expected ABI so a bump never collides with a hard-coded "mismatch" value
  // (the historical 0x00010001 stamp became the expected ABI itself after the P2 bump).
  for (uint32_t stamped :
       {0x00000000u, ExpectedToolchainABI - 1u, ExpectedToolchainABI + 1u, 0xFFFFFFFFu}) {
    SCOPED_TRACE(stamped);
    auto bundle = MakeTestBundle("toolchain", 1, 0, 50);
    TestWriteU32LE(bundle.data() + 16, stamped);
    ExpectBundleRejectedPreservingCache(bundle);
  }
  // The matching ABI still loads and serves entries.
  auto valid = MakeTestBundle("toolchain", 1, 0, 50);
  TestWriteU32LE(valid.data() + 16, ExpectedToolchainABI);
  PrecompiledShaderCache cache(nullptr, Backend::Unknown);
  EXPECT_TRUE(cache.loadBundle(valid.data(), valid.size()));
  EXPECT_NE(cache.findVertex(50), nullptr);
}

TGFX_TEST(ShaderPermutationTest, BundleRejectsUnknownPayloadFormat) {
  // The payload kind decides which ShaderCodeFormat the blobs are handed to the backend as, so a
  // value past the last defined form names nothing the loader could pick.
  for (uint16_t stamped : {uint16_t{BundleFormat::Payload::Max + 1}, uint16_t{0xFFFF}}) {
    SCOPED_TRACE(stamped);
    auto bundle = MakeTestBundle("payload", 1, 0, 50);
    TestWriteU16LE(bundle.data() + BundleFormat::Offset::PayloadFormat, stamped);
    ExpectBundleRejectedPreservingCache(bundle);
  }
  // Both defined forms load: the field states what the blobs hold, it does not gate the profile.
  for (uint16_t stamped : {BundleFormat::Payload::Text, BundleFormat::Payload::Binary}) {
    SCOPED_TRACE(stamped);
    auto bundle = MakeTestBundle("payload", 1, 0, 50);
    TestWriteU16LE(bundle.data() + BundleFormat::Offset::PayloadFormat, stamped);
    PrecompiledShaderCache cache(nullptr, Backend::Unknown);
    ASSERT_TRUE(cache.loadBundle(bundle.data(), bundle.size()));
    EXPECT_EQ(cache.payloadFormat(), stamped);
    EXPECT_NE(cache.findVertex(50), nullptr);
  }
}

TGFX_TEST(ShaderPermutationTest, BundleRejectsAllocationAmplification) {
  constexpr uint32_t BlobSize = 1024u * 1024u;
  constexpr uint32_t EntryCount = 257;
  auto bundle = MakeTestBundle("bounds", EntryCount, 0, 50);
  auto dataOffset = TestReadU32LE(bundle.data() + 36);
  bundle.resize(static_cast<size_t>(dataOffset) + BlobSize, 1);
  TestWriteU32LE(bundle.data() + 40, BlobSize);
  SetTestBundleIdentity(&bundle);
  PrecompiledShaderCache cache(nullptr, Backend::Unknown);
  ASSERT_TRUE(cache.loadBundle(bundle.data(), bundle.size()));
  for (uint32_t index = 0; index < EntryCount; ++index) {
    auto entryOffset = BundleFormat::HeaderSize + index * BundleFormat::PoolEntrySize;
    TestWriteU32LE(bundle.data() + entryOffset + BundleFormat::EntryOffset::DataOffset, 0);
    TestWriteU32LE(bundle.data() + entryOffset + BundleFormat::EntryOffset::DataSize, BlobSize);
  }
  auto generation = cache.bundleGeneration();
  EXPECT_FALSE(cache.loadBundle(bundle.data(), bundle.size()));
  EXPECT_EQ(cache.bundleGeneration(), generation);
  EXPECT_EQ(cache.vertexEntryCount(), EntryCount);
  auto tooManyEntries = MakeTestBundle("bounds", 65537, 0, 50);
  ExpectBundleRejectedPreservingCache(tooManyEntries);
}

TGFX_TEST(ShaderPermutationTest, BundleRejectsReconstructedSizeOverflow) {
  // The uncompressed path rejects a declared data pool running past the file without any
  // allocation; the u32 wraparound value exercises the same size_t-domain check.
  auto invalid = MakeReflectedTestBundle(BundleFormat::CurrentVersion);
  TestWriteU32LE(invalid.data() + 40, 200);
  ExpectBundleRejectedPreservingCache(invalid);
  TestWriteU32LE(invalid.data() + 40, std::numeric_limits<uint32_t>::max());
  ExpectBundleRejectedPreservingCache(invalid);
  for (uint16_t compression : {uint16_t{2}}) {
    auto compressed =
        CompressTestBundle(MakeReflectedTestBundle(BundleFormat::CurrentVersion), compression);
    ASSERT_FALSE(compressed.empty());
    auto dataOffset = TestReadU32LE(compressed.data() + 36);
    for (uint32_t size : {3u, 256u * 1024u * 1024u, 256u * 1024u * 1024u - dataOffset,
                          std::numeric_limits<uint32_t>::max()}) {
      TestWriteU32LE(compressed.data() + 40, size);
      ExpectBundleRejectedPreservingCache(compressed);
    }
  }
}

TGFX_TEST(ShaderPermutationTest, CompressedBundleRejectsIncorrectDataSize) {
  for (uint16_t compression = 2; compression <= 2; ++compression) {
    auto original =
        CompressTestBundle(MakeReflectedTestBundle(BundleFormat::CurrentVersion), compression);
    ASSERT_FALSE(original.empty());
    for (uint32_t size : {1u, 3u}) {
      auto invalid = original;
      TestWriteU32LE(invalid.data() + 40, size);
      ExpectBundleRejectedPreservingCache(invalid);
    }
    auto truncated = original;
    auto reflectionOffset = TestReadU32LE(truncated.data() + 44);
    truncated.erase(truncated.begin() + reflectionOffset - 1);
    TestWriteU32LE(truncated.data() + 44, reflectionOffset - 1);
    ExpectBundleRejectedPreservingCache(truncated);
  }
}

TGFX_TEST(ShaderPermutationTest, CompressedBundleFailedReflectionPreservesCache) {
  for (uint16_t compression = 2; compression <= 2; ++compression) {
    auto invalid =
        CompressTestBundle(MakeReflectedTestBundle(BundleFormat::CurrentVersion), compression);
    ASSERT_FALSE(invalid.empty());
    auto fragPoolOffset = TestReadU32LE(invalid.data() + 32);
    TestWriteU32LE(invalid.data() + fragPoolOffset + 24, std::numeric_limits<uint32_t>::max());
    ExpectBundleRejectedPreservingCache(invalid);
  }
}

TGFX_TEST(ShaderPermutationTest, CompressedBundleRejectsInvalidOffsetOrder) {
  for (uint16_t compression : {uint16_t{2}}) {
    auto bundle =
        CompressTestBundle(MakeReflectedTestBundle(BundleFormat::CurrentVersion), compression);
    PrecompiledShaderCache cache(nullptr, Backend::Unknown);
    ASSERT_TRUE(cache.loadBundle(bundle.data(), bundle.size()));
    auto generation = cache.bundleGeneration();
    auto dataOffset = TestReadU32LE(bundle.data() + 36);
    TestWriteU32LE(bundle.data() + 44, dataOffset - 1);
    EXPECT_FALSE(cache.loadBundle(bundle.data(), bundle.size()));
    EXPECT_EQ(cache.bundleGeneration(), generation);
    EXPECT_TRUE(cache.isLoaded());
  }
}

TGFX_TEST(ShaderPermutationTest, FailedBundleReloadPreservesCache) {
  auto original = MakeTestBundle("original", 1, 1, 10);
  PrecompiledShaderCache cache(nullptr, Backend::Unknown);
  ASSERT_TRUE(cache.loadBundle(original.data(), original.size()));
  ASSERT_TRUE(cache.findVertex(10) != nullptr);
  ASSERT_TRUE(cache.findFragment(10 + 0x100) != nullptr);

  auto invalid = MakeTestBundle("failed", 2, 1, 30);
  TestWriteU32LE(invalid.data() + 32, static_cast<uint32_t>(invalid.size() - 1));
  EXPECT_FALSE(cache.loadBundle(invalid.data(), invalid.size()));
  EXPECT_EQ(cache.vertexEntryCount(), 1u);
  EXPECT_EQ(cache.fragmentEntryCount(), 1u);
  EXPECT_EQ(cache.profileTag(), "original");
  EXPECT_TRUE(cache.findVertex(10) != nullptr);
  EXPECT_TRUE(cache.findFragment(10 + 0x100) != nullptr);
  EXPECT_TRUE(cache.findVertex(30) == nullptr);
}

TGFX_TEST(ShaderPermutationTest, SuccessfulBundleReloadReplacesCache) {
  auto original = MakeTestBundle("original", 2, 2, 10);
  PrecompiledShaderCache cache(nullptr, Backend::Unknown);
  ASSERT_TRUE(cache.loadBundle(original.data(), original.size()));
  ASSERT_EQ(cache.vertexEntryCount(), 2u);
  ASSERT_EQ(cache.fragmentEntryCount(), 2u);

  auto replacement = MakeTestBundle("replacement", 1, 1, 30);
  ASSERT_TRUE(cache.loadBundle(replacement.data(), replacement.size()));
  EXPECT_TRUE(cache.isLoaded());
  EXPECT_EQ(cache.vertexEntryCount(), 1u);
  EXPECT_EQ(cache.fragmentEntryCount(), 1u);
  EXPECT_EQ(cache.profileTag(), "replacement");
  EXPECT_TRUE(cache.findVertex(30) != nullptr);
  EXPECT_TRUE(cache.findFragment(30 + 0x100) != nullptr);
  EXPECT_TRUE(cache.findVertex(10) == nullptr);
  EXPECT_TRUE(cache.findFragment(10 + 0x100) == nullptr);
}

TGFX_TEST(ShaderPermutationTest, CreatorFunnelRecordsArtifactMiss) {
  auto image = MakeImage("resources/apitest/test_timestretch.png");
  ASSERT_TRUE(image != nullptr);
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  if (context->backend() == Backend::OpenGL) {
    GTEST_SKIP() << "plain texture fill is outside the OpenGL stage 1 whitelist";
  }
  auto surface = Surface::Make(context, 200, 200);
  ASSERT_TRUE(surface != nullptr);
  auto* cache = ContextCacheAccess::Cache(context);
  // The fake bundle loaded below must not outlive this test: the thread-local Context (and with it
  // this cache) is shared by every later ContextScope of the suite, and a draw against the fake
  // bundle turns their hits into VertexArtifactMissing misses.
  ScopedEmbeddedBundleReload reloadEmbedded(cache, context->backend());
  image = MakeTexture2DImage(context, image);
  ASSERT_TRUE(image != nullptr);
  auto expectedProfile = ExpectedShaderProfile(context->backend());
  ASSERT_TRUE(expectedProfile.has_value());
  const std::string expectedTag = ProfileTag(*expectedProfile);
  auto unrelatedBundle = MakeTestBundle(expectedTag, 1, 1, 10);
  ASSERT_TRUE(cache->loadBundle(unrelatedBundle.data(), unrelatedBundle.size()));
  EXPECT_EQ(cache->profileTag(), expectedTag);
  context->globalCache()->clearPrograms();
  context->globalCache()->resetProgramStats();
  cache->resetStats();
  cache->setDiagnosticRecordingEnabled(true);
  // This miss is provoked on purpose to verify the accounting; mark it so the production
  // coverage metrics exclude it.
  cache->setDeliberateMissMarking(true);

  surface->getCanvas()->drawImage(image, 0, 0);
  context->flushAndSubmit(true);
  cache->setDeliberateMissMarking(false);

  EXPECT_EQ(cache->aotStageCount(PrecompiledAOTStage::Attempt), 1u);
  EXPECT_EQ(cache->aotStageCount(PrecompiledAOTStage::CacheAvailable), 1u);
  EXPECT_EQ(cache->aotStageCount(PrecompiledAOTStage::PermutationMatched), 1u);
  EXPECT_EQ(cache->aotStageCount(PrecompiledAOTStage::ArtifactsFound), 0u);
  EXPECT_EQ(cache->aotStageCount(PrecompiledAOTStage::VertexModuleCreated), 0u);
  EXPECT_EQ(cache->aotStageCount(PrecompiledAOTStage::FragmentModuleCreated), 0u);
  EXPECT_EQ(cache->aotStageCount(PrecompiledAOTStage::PipelineCreated), 0u);
  EXPECT_EQ(cache->hitCount(), 0u);
  EXPECT_EQ(cache->missCount(), 1u);
  EXPECT_EQ(cache->fallbackCount(PrecompiledFallbackReason::VertexArtifactMissing), 1u);
  auto fallbackRecords = cache->fallbackRecords();
  EXPECT_EQ(fallbackRecords.size(), 1u);
  if (!fallbackRecords.empty()) {
    EXPECT_EQ(fallbackRecords[0].reason, PrecompiledFallbackReason::VertexArtifactMissing);
    EXPECT_TRUE(fallbackRecords[0].deliberate);
    EXPECT_FALSE(fallbackRecords[0].shaderName.empty());
    EXPECT_FALSE(fallbackRecords[0].effectSignature.empty());
    EXPECT_FALSE(fallbackRecords[0].pipelineSignature.empty());
  }
  const auto& programStats = context->globalCache()->programStats();
  EXPECT_EQ(programStats.cacheMisses, 1u);
  EXPECT_EQ(programStats.precompiledArtifactCreations, 0u);

  cache->setDiagnosticRecordingEnabled(false);
}

TGFX_TEST(ShaderPermutationTest, ProgramMissCacheClearsOnBundleChange) {
  // The negative cache must never outlive the bundle it was recorded against: a new bundle can
  // serve a key the old one refused.
  PrecompiledShaderCache cache(nullptr, Backend::Unknown);
  BytesKey key;
  key.write(7);
  PrecompiledFallbackReason reason = PrecompiledFallbackReason::Unspecified;
  EXPECT_FALSE(cache.findProgramMiss(key, &reason));
  cache.recordProgramMiss(key, PrecompiledFallbackReason::NoMatchingRule);
  ASSERT_TRUE(cache.findProgramMiss(key, &reason));
  EXPECT_EQ(reason, PrecompiledFallbackReason::NoMatchingRule);

  auto bundle = MakeTestBundle("bounds", 1, 1, 50);
  ASSERT_TRUE(cache.loadBundle(bundle.data(), bundle.size()));
  EXPECT_FALSE(cache.findProgramMiss(key, &reason));

  cache.recordProgramMiss(key, PrecompiledFallbackReason::VertexArtifactMissing);
  ASSERT_TRUE(cache.findProgramMiss(key, &reason));
  ASSERT_TRUE(cache.loadBundle(bundle.data(), bundle.size()));
  EXPECT_FALSE(cache.findProgramMiss(key, &reason));
}

TGFX_TEST(ShaderPermutationTest, ARefusedProgramKeyIsNotRematched) {
  // The negative cache's contract: the first draw with a refused program key walks the rules and
  // records the fallback; a later draw with the same key is refused from the cache — counted as a
  // miss, but neither re-matched nor recorded again.
  auto image = MakeImage("resources/apitest/imageReplacement.png");
  ASSERT_TRUE(image != nullptr);
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 128, 128);
  ASSERT_TRUE(surface != nullptr);
  auto* cache = ContextCacheAccess::Cache(context);
  cache->setDiagnosticRecordingEnabled(true);
  auto drawOnce = [&]() {
    // The program cache would otherwise serve the second draw without ever asking the creator.
    context->globalCache()->clearPrograms();
    surface->getCanvas()->drawImage(image, 0, 0);
    context->flushAndSubmit(true);
  };
  drawOnce();
  if (cache->missCount() == 0) {
    cache->setDiagnosticRecordingEnabled(false);
    GTEST_SKIP() << "the bundle serves this draw on this backend";
  }
  const auto recordsAfterFirst = cache->fallbackRecords().size();
  const auto missesAfterFirst = cache->missCount();
  drawOnce();
  EXPECT_GT(cache->missCount(), missesAfterFirst);
  EXPECT_EQ(cache->fallbackRecords().size(), recordsAfterFirst);
  EXPECT_EQ(cache->aotStageCount(PrecompiledAOTStage::CacheAvailable), 2u);
  cache->setDiagnosticRecordingEnabled(false);
}

TGFX_TEST(ShaderPermutationTest, CompressedBundleLoad) {
  // Load an uncompressed bundle, manually compress its data pool, then verify loading.
  auto original = CopyEmbeddedBundle(TestBackend());
  if (original.empty()) {
    GTEST_SKIP() << "This build embeds no bundle for the backend";
    return;
  }

  // Production bundles ship compressed; when the resource bundle is already compressed, the
  // load itself is the roundtrip coverage, so just verify entries and tag.
  if (TestReadU16LE(original.data() + 6) != 0u) {
    PrecompiledShaderCache compressedOnly(nullptr, TestBackend());
    ASSERT_TRUE(compressedOnly.loadBundle(original.data(), original.size()));
    EXPECT_TRUE(compressedOnly.isLoaded());
    std::string tag = BundleTag();
    ExpectBundleEntryCounts(compressedOnly, tag);
    EXPECT_EQ(compressedOnly.profileTag(), tag);
    return;
  }

  uint32_t dataOffset = TestReadU32LE(original.data() + 36);
  uint32_t dataSize = TestReadU32LE(original.data() + 40);
  uint32_t reflectionOffset = TestReadU32LE(original.data() + 44);
  ASSERT_GT(dataSize, 0u);

  // Compress the data pool section.
  std::vector<uint8_t> compressedData(ZSTD_compressBound(dataSize));
  auto compSize = ZSTD_compress(compressedData.data(), compressedData.size(),
                                original.data() + dataOffset, dataSize, 1);
  ASSERT_FALSE(ZSTD_isError(compSize));
  compressedData.resize(compSize);

  // Build a new bundle: [header+pools | compressed data | reflection]
  std::vector<uint8_t> compressed;
  compressed.insert(compressed.end(), original.begin(), original.begin() + dataOffset);
  compressed.insert(compressed.end(), compressedData.begin(), compressedData.end());
  if (reflectionOffset > 0) {
    compressed.insert(compressed.end(), original.begin() + reflectionOffset, original.end());
  }

  // Patch header: set compressionType=2 (zstd) and update reflectionOffset.
  TestWriteU16LE(compressed.data() + 6, 2);
  if (reflectionOffset > 0) {
    uint32_t newReflOffset = static_cast<uint32_t>(dataOffset + compSize);
    compressed[44] = static_cast<uint8_t>(newReflOffset & 0xFF);
    compressed[45] = static_cast<uint8_t>((newReflOffset >> 8) & 0xFF);
    compressed[46] = static_cast<uint8_t>((newReflOffset >> 16) & 0xFF);
    compressed[47] = static_cast<uint8_t>((newReflOffset >> 24) & 0xFF);
  }

  // Load the compressed bundle.
  PrecompiledShaderCache compressedCache(nullptr, TestBackend());
  ASSERT_TRUE(compressedCache.loadBundle(compressed.data(), compressed.size()));
  EXPECT_TRUE(compressedCache.isLoaded());

  // Compare with uncompressed load.
  PrecompiledShaderCache originalCache(nullptr, TestBackend());
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
  if (context->backend() == Backend::OpenGL) {
    GTEST_SKIP() << "plain texture fill is outside the OpenGL stage 1 whitelist";
  }
  auto surface = Surface::Make(context, 200, 200);
  ASSERT_TRUE(surface != nullptr);
  auto* cache = ContextCacheAccess::Cache(context);
  image = MakeTexture2DImage(context, image);
  ASSERT_TRUE(image != nullptr);
  ASSERT_TRUE(LoadEmbeddedBundle(cache, context->backend()));
  context->globalCache()->clearPrograms();
  context->globalCache()->resetProgramStats();
  cache->resetStats();
  cache->setDiagnosticRecordingEnabled(true);

  surface->getCanvas()->drawImage(image, 0, 0);
  context->flushAndSubmit(true);

  EXPECT_EQ(cache->aotStageCount(PrecompiledAOTStage::Attempt), 1u);
  EXPECT_EQ(cache->aotStageCount(PrecompiledAOTStage::CacheAvailable), 1u);
  EXPECT_EQ(cache->aotStageCount(PrecompiledAOTStage::PermutationMatched), 1u);
  EXPECT_EQ(cache->aotStageCount(PrecompiledAOTStage::ArtifactsFound), 1u);
  EXPECT_EQ(cache->aotStageCount(PrecompiledAOTStage::VertexModuleCreated), 1u);
  EXPECT_EQ(cache->aotStageCount(PrecompiledAOTStage::FragmentModuleCreated), 1u);
  EXPECT_EQ(cache->aotStageCount(PrecompiledAOTStage::PipelineCreated), 1u);
  EXPECT_EQ(cache->hitCount(), 1u);
  EXPECT_EQ(cache->missCount(), 0u);
  auto hitRecords = cache->hitRecords();
  EXPECT_EQ(hitRecords.size(), 1u);
  if (!hitRecords.empty()) {
    EXPECT_FALSE(hitRecords[0].shaderName.empty());
    EXPECT_FALSE(hitRecords[0].effectSignature.empty());
    EXPECT_FALSE(hitRecords[0].pipelineSignature.empty());
  }
  const auto& programStats = context->globalCache()->programStats();
  EXPECT_EQ(programStats.cacheMisses, 1u);
  EXPECT_EQ(programStats.precompiledArtifactCreations, 1u);

  cache->setDiagnosticRecordingEnabled(false);
}

TGFX_TEST(ShaderPermutationTest, AlphaThresholdHitsPrecompiledCache) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  if (context->backend() == Backend::OpenGL) {
    GTEST_SKIP() << "AlphaThreshold is outside the OpenGL stage 1 whitelist";
  }
  auto* cache = ContextCacheAccess::Cache(context);
  ASSERT_TRUE(LoadEmbeddedBundle(cache, context->backend()));
  cache->resetStats();
  auto surface = Surface::Make(context, 100, 100);
  ASSERT_TRUE(surface != nullptr);
  Paint paint;
  paint.setColor(Color::FromRGBA(100, 0, 0, 128));
  paint.setColorFilter(ColorFilter::AlphaThreshold(0.5f));
  surface->getCanvas()->drawRect(Rect::MakeWH(100, 100), paint);
  context->flushAndSubmit(true);
  EXPECT_GT(cache->hitCount(), 0u);
}

TGFX_TEST(ShaderPermutationTest, LumaHitsPrecompiledCache) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  if (context->backend() == Backend::OpenGL) {
    GTEST_SKIP() << "standalone Luma is outside the OpenGL stage 1 whitelist";
  }
  auto* cache = ContextCacheAccess::Cache(context);
  ASSERT_TRUE(LoadEmbeddedBundle(cache, context->backend()));
  cache->resetStats();
  auto surface = Surface::Make(context, 100, 100);
  ASSERT_TRUE(surface != nullptr);
  Paint paint;
  paint.setColor(Color::FromRGBA(125, 0, 255));
  paint.setColorFilter(ColorFilter::Luma());
  surface->getCanvas()->drawRect(Rect::MakeWH(100, 100), paint);
  context->flushAndSubmit(true);
  EXPECT_GT(cache->hitCount(), 0u);
}

TGFX_TEST(ShaderPermutationTest, GaussianBlurHitsPrecompiledCache) {
  auto image = MakeImage("resources/apitest/image_as_mask.png");
  ASSERT_TRUE(image != nullptr);
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  if (context->backend() == Backend::OpenGL) {
    GTEST_SKIP() << "GaussianBlur is outside the OpenGL stage 1 whitelist";
  }
  auto* cache = ContextCacheAccess::Cache(context);
  ASSERT_TRUE(LoadEmbeddedBundle(cache, context->backend()));
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
}

TGFX_TEST(ShaderPermutationTest, ChainBlendClipHitsPrecompiledCache) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  if (context->backend() == Backend::OpenGL) {
    GTEST_SKIP() << "The pointwise chain is outside the OpenGL stage 1 whitelist";
  }
  auto* cache = ContextCacheAccess::Cache(context);
  ASSERT_TRUE(LoadEmbeddedBundle(cache, context->backend()));
  cache->resetStats();
  auto surface = Surface::Make(context, 200, 200);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();
  // Draw with ColorFilter::Blend (produces XfermodeFragmentProcessor) + AA clip rect
  // (produces a device-space RectEffect as coverage FP). The pointwise chain serves this with an
  // AARectCoverage slot.
  canvas->save();
  canvas->clipRect(Rect::MakeLTRB(10.5f, 10.5f, 189.5f, 189.5f));
  Paint paint;
  paint.setColor(Color::Green());
  paint.setColorFilter(ColorFilter::Blend(Color::Blue(), BlendMode::Multiply));
  canvas->drawRect(Rect::MakeWH(200, 200), paint);
  canvas->restore();
  context->flushAndSubmit(true);
  printf("  [ChainBlendClip] hit=%u miss=%u\n", cache->hitCount(), cache->missCount());
  EXPECT_GT(cache->hitCount(), 0u);
}

TGFX_TEST(ShaderPermutationTest, MaskFillHitsPrecompiledCache) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  if (context->backend() == Backend::OpenGL) {
    GTEST_SKIP() << "MaskFill is outside the OpenGL stage 1 whitelist";
  }
  auto* cache = ContextCacheAccess::Cache(context);
  ASSERT_TRUE(LoadEmbeddedBundle(cache, context->backend()));
  // hitRecords() is only populated when diagnostic recording is enabled.
  cache->setDiagnosticRecordingEnabled(true);
  cache->resetStats();
  auto surface = Surface::Make(context, 200, 200);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();
  // A small filled path is rasterized to an alpha-only shape mask by ShapeDrawOp and drawn through
  // DefaultGeometryProcessor with a single coverage TextureEffect — the MaskFillShader case.
  Path path;
  path.moveTo(20, 20);
  path.lineTo(180, 40);
  path.lineTo(100, 180);
  path.close();
  Paint paint;
  paint.setColor(Color::Red());
  canvas->drawPath(path, paint);
  context->flushAndSubmit(true);
  bool hitMaskFill = false;
  for (const auto& record : cache->hitRecords()) {
    if (record.shaderName == "MaskFill") {
      hitMaskFill = true;
      break;
    }
  }
  printf("  [MaskFill] hit=%u miss=%u maskHit=%d\n", cache->hitCount(), cache->missCount(),
         hitMaskFill);
  EXPECT_TRUE(hitMaskFill);
  // Read back a pixel inside the triangle (centroid ~ (100, 80)) to check the fill actually renders.
  Bitmap bitmap(surface->width(), surface->height(), false, false, surface->colorSpace());
  Pixmap pixmap(bitmap);
  if (surface->readPixels(pixmap.info(), pixmap.writablePixels())) {
    auto* px = static_cast<const uint8_t*>(pixmap.pixels());
    size_t idx = (80 * static_cast<size_t>(pixmap.rowBytes())) + 100 * 4;
    printf("  [MaskFill] centerPixel RGBA = %u,%u,%u,%u\n", px[idx], px[idx + 1], px[idx + 2],
           px[idx + 3]);
  }
}

TGFX_TEST(ShaderPermutationTest, QuadTextureFillShaderRegistry) {
  // The family is declaration-driven now: it has no factory shell and no source files, and its
  // domain spans the geometry's HAS_UV_COORD/HAS_SUBSET, Texture's HAS_SUBSET, LocalMask's
  // PRESENT and Xfer's HAS_XP (2 * 2 * 2 * 2 * 3); the product rule prunes the enumerated bundle
  // to the 24 real programs.
  const auto* registered = FindShaderInfo("QuadTexture");
  ASSERT_NE(registered, nullptr);
  EXPECT_EQ(registered->vertDomain.totalCount(), 48u);
  EXPECT_EQ(registered->fragDomain.totalCount(), 48u);
  EXPECT_TRUE(registered->vertexFile.empty());
  EXPECT_TRUE(registered->fragmentFile.empty());
}

TGFX_TEST(ShaderPermutationTest, ShapeInstancedTextureCoverageMatchesOnlyExactContract) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  // This is a matcher-only test: the family is compiled for the OpenGL profile too (the full-suite
  // AOT report serves it), so the former "stage 1 whitelist" skip no longer applies.
  auto renderTargetProxy = RenderTargetProxy::Make(context, 8, 8, false);
  ASSERT_NE(renderTargetProxy, nullptr);
  auto textureProxy = context->proxyProvider()->createTextureProxy({}, 8, 8, PixelFormat::ALPHA_8);
  ASSERT_NE(textureProxy, nullptr);
  ASSERT_NE(textureProxy->getTextureView(), nullptr);

  {
    BlockAllocator allocator;
    auto gp = ShapeInstancedGeometryLayout(AAType::None, true, Matrix::I(), Matrix::I());
    auto coverage = TextureEffect::Make(&allocator, textureProxy);
    ASSERT_NE(coverage, nullptr);
    ProgramInfo programInfo(renderTargetProxy->getRenderTarget().get(), gp, std::nullopt,
                            BlendMode::SrcOver, false);
    auto ir = LowerDraw({}, {coverage.get()}, BlendMode::SrcOver, &allocator);
    ASSERT_TRUE(ir.has_value());
    programInfo.setCarriedIR(&*ir);
    auto match = SelectProgram(context, programInfo, SelectionScope::AnyFamily);
    ASSERT_TRUE(match.has_value());
    EXPECT_EQ(Families()[match->variant.family].name, "ShapeInstancedTextureCoverage");
    // The bare coverage form is GRADIENT=0, and the family's allowed rule keeps VERTEX_COLOR and
    // HAS_XP at 0, so the ordinal is the gradient radix alone.
    EXPECT_EQ(match->variant.ordinal, 0u);
  }

  {
    // The colorless bare form matches too: the instance color attribute is unconditional and
    // colorless batches broadcast white, which is exactly the runtime codegen's implicit color,
    // so the former rejection is gone and the draw picks the GRADIENT=0 variant.
    BlockAllocator allocator;
    auto gp = ShapeInstancedGeometryLayout(AAType::None, false, Matrix::I(), Matrix::I());
    auto coverage = TextureEffect::Make(&allocator, textureProxy);
    ASSERT_NE(coverage, nullptr);
    ProgramInfo programInfo(renderTargetProxy->getRenderTarget().get(), gp, std::nullopt,
                            BlendMode::SrcOver, false);
    auto ir = LowerDraw({}, {coverage.get()}, BlendMode::SrcOver, &allocator);
    ASSERT_TRUE(ir.has_value());
    programInfo.setCarriedIR(&*ir);
    auto match = SelectProgram(context, programInfo, SelectionScope::AnyFamily);
    ASSERT_TRUE(match.has_value());
    EXPECT_EQ(Families()[match->variant.family].name, "ShapeInstancedTextureCoverage");
    EXPECT_EQ(match->variant.ordinal, 0u);
  }

  {
    BlockAllocator allocator;
    auto gp = ShapeInstancedGeometryLayout(AAType::Coverage, true, Matrix::I(), Matrix::I());
    auto coverage = TextureEffect::Make(&allocator, textureProxy);
    ASSERT_NE(coverage, nullptr);
    ProgramInfo programInfo(renderTargetProxy->getRenderTarget().get(), gp, std::nullopt,
                            BlendMode::SrcOver, false);
    auto ir = LowerDraw({}, {coverage.get()}, BlendMode::SrcOver, &allocator);
    ASSERT_TRUE(ir.has_value());
    programInfo.setCarriedIR(&*ir);
    EXPECT_FALSE(SelectProgram(context, programInfo, SelectionScope::AnyFamily).has_value());
  }

  {
    BlockAllocator allocator;
    auto gp = ShapeInstancedGeometryLayout(AAType::None, true, Matrix::I(), Matrix::I());
    auto color = ConstColorProcessor::Make(&allocator, PMColor::Red(), InputMode::Ignore);
    auto coverage = TextureEffect::Make(&allocator, textureProxy);
    ASSERT_NE(color, nullptr);
    ASSERT_NE(coverage, nullptr);
    ProgramInfo programInfo(renderTargetProxy->getRenderTarget().get(), gp, std::nullopt,
                            BlendMode::SrcOver, false);
    auto ir = LowerDraw({color.get()}, {coverage.get()}, BlendMode::SrcOver, &allocator);
    ASSERT_TRUE(ir.has_value());
    programInfo.setCarriedIR(&*ir);
    EXPECT_FALSE(SelectProgram(context, programInfo, SelectionScope::AnyFamily).has_value());
  }

  {
    BlockAllocator allocator;
    auto gp = ShapeInstancedGeometryLayout(AAType::None, true, Matrix::I(), Matrix::I());
    auto coverage = TextureEffect::MakeRGBAAA(&allocator, textureProxy, {}, Point{1, 0});
    ASSERT_NE(coverage, nullptr);
    ProgramInfo programInfo(renderTargetProxy->getRenderTarget().get(), gp, std::nullopt,
                            BlendMode::SrcOver, false);
    auto ir = LowerDraw({}, {coverage.get()}, BlendMode::SrcOver, &allocator);
    ASSERT_TRUE(ir.has_value());
    programInfo.setCarriedIR(&*ir);
    EXPECT_FALSE(SelectProgram(context, programInfo, SelectionScope::AnyFamily).has_value());
  }

  {
    BlockAllocator allocator;
    auto gp = ShapeInstancedGeometryLayout(AAType::None, true, Matrix::I(), Matrix::I());
    auto coverage = TextureEffect::Make(&allocator, textureProxy);
    // A present value with an empty proxy: the framebuffer-fetch state.
    std::optional<DstTextureInfo> dst = DstTextureInfo{};
    ASSERT_NE(coverage, nullptr);
    ProgramInfo programInfo(renderTargetProxy->getRenderTarget().get(), gp, dst, BlendMode::SrcOver,
                            false);
    auto ir = LowerDraw({}, {coverage.get()}, BlendMode::SrcOver, &allocator);
    ASSERT_TRUE(ir.has_value());
    programInfo.setCarriedIR(&*ir);
    EXPECT_FALSE(SelectProgram(context, programInfo, SelectionScope::AnyFamily).has_value());
  }

  // A rounded clip next to the coverage texture: the clip rides the runtime clip contract uniforms,
  // so the draw selects the same variant as the bare-coverage form (the libpag corpus carries this
  // shape; before the contract it had no precompiled route at all).
  const auto expectedIndex = 0u;
  for (int clipFirst = 0; clipFirst <= 1; ++clipFirst) {
    BlockAllocator allocator;
    auto gp = ShapeInstancedGeometryLayout(AAType::None, true, Matrix::I(), Matrix::I());
    auto coverage = TextureEffect::Make(&allocator, textureProxy);
    RRect rRect = {};
    rRect.setRectXY(Rect::MakeXYWH(0, 0, 8, 8), 3, 3);
    auto clip = RRectEffect::Make(&allocator, rRect, Matrix::I(), true);
    ASSERT_NE(coverage, nullptr);
    ASSERT_NE(clip, nullptr);
    std::vector<FragmentProcessor*> processors =
        clipFirst != 0 ? std::vector<FragmentProcessor*>{clip.get(), coverage.get()}
                       : std::vector<FragmentProcessor*>{coverage.get(), clip.get()};
    ProgramInfo programInfo(renderTargetProxy->getRenderTarget().get(), gp, std::nullopt,
                            BlendMode::SrcOver, false);
    // The coverage classification reads the carried IR's terms: lower the same expression the op
    // would have lowered at record time and carry it, as the plain route's prepare does. The
    // geometry and xfer facts the matchers read come off the made processors at match time.
    std::vector<const FragmentProcessor*> coverageList(processors.begin(), processors.end());
    auto ir = LowerDraw({}, coverageList, BlendMode::SrcOver, &allocator);
    ASSERT_TRUE(ir.has_value());
    programInfo.setCarriedIR(&*ir);
    auto failure = PermutationMatchFailure::CoverageShape;
    auto match = SelectProgram(context, programInfo, SelectionScope::AnyFamily, &failure);
    ASSERT_TRUE(match.has_value()) << "clipFirst=" << clipFirst;
    EXPECT_EQ(failure, PermutationMatchFailure::None);
    EXPECT_EQ(Families()[match->variant.family].name, "ShapeInstancedTextureCoverage");
    EXPECT_EQ(match->variant.ordinal, expectedIndex);
  }

  // Two masks (no clip) are still not a shape this kernel evaluates.
  {
    BlockAllocator allocator;
    auto gp = ShapeInstancedGeometryLayout(AAType::None, true, Matrix::I(), Matrix::I());
    auto coverage = TextureEffect::Make(&allocator, textureProxy);
    auto secondCoverage = TextureEffect::Make(&allocator, textureProxy);
    ASSERT_NE(coverage, nullptr);
    ASSERT_NE(secondCoverage, nullptr);
    ProgramInfo programInfo(renderTargetProxy->getRenderTarget().get(), gp, std::nullopt,
                            BlendMode::SrcOver, false);
    auto twoMaskIR = LowerDraw({},
                               {static_cast<const FragmentProcessor*>(coverage.get()),
                                static_cast<const FragmentProcessor*>(secondCoverage.get())},
                               BlendMode::SrcOver, &allocator);
    ASSERT_TRUE(twoMaskIR.has_value());
    programInfo.setCarriedIR(&*twoMaskIR);
    auto failure = PermutationMatchFailure::None;
    EXPECT_FALSE(
        SelectProgram(context, programInfo, SelectionScope::AnyFamily, &failure).has_value());
    // The rejection itself is the contract. The reason is a diagnostic classification that
    // collapses to NoMatchingRule for the fallback report, so asserting one specific sub-reason
    // would freeze an internal detail of the family's predicate shape.
    EXPECT_TRUE(failure == PermutationMatchFailure::CoverageShape ||
                failure == PermutationMatchFailure::NoMatchingRule);
  }
}

// The dedicated ComplexEllipseFill kernel rejects a texture-coverage draw, and the interpreter
// (whose walk entry comes after every dedicated family) now serves it instead: the shape is no
// longer a miss, so the draw renders rather than depending on the prepare-time rewrite.
TGFX_TEST(ShaderPermutationTest, ComplexEllipseTextureCoverageIsServedByChain) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto renderTargetProxy = RenderTargetProxy::Make(context, 8, 8, false);
  ASSERT_NE(renderTargetProxy, nullptr);
  auto renderTarget = renderTargetProxy->getRenderTarget();
  ASSERT_NE(renderTarget, nullptr);
  auto textureProxy = context->proxyProvider()->createTextureProxy({}, 8, 8, PixelFormat::ALPHA_8);
  ASSERT_NE(textureProxy, nullptr);
  ASSERT_NE(textureProxy->getTextureView(), nullptr);
  BlockAllocator allocator;
  auto gp = ComplexEllipseGeometryLayout(false, PMColor::White());
  auto coverage = TextureEffect::Make(&allocator, textureProxy);
  ASSERT_NE(coverage, nullptr);
  auto ir = LowerDraw({}, {coverage.get()}, BlendMode::SrcOver, &allocator);
  ASSERT_TRUE(ir.has_value());
  ProgramInfo rejected(renderTarget.get(), gp, std::nullopt, BlendMode::SrcOver, false);
  rejected.setCarriedIR(&*ir);
  auto failure = PermutationMatchFailure::None;
  auto served = SelectProgram(context, rejected, SelectionScope::AnyFamily, &failure);
  ASSERT_TRUE(served.has_value());
  EXPECT_STREQ(Families()[served->variant.family].name, "Chain");

  ProgramInfo accepted(renderTarget.get(), gp, std::nullopt, BlendMode::SrcOver, false);
  auto match = SelectProgram(context, accepted, SelectionScope::AnyFamily, &failure);
  ASSERT_TRUE(match.has_value());
  EXPECT_EQ(Families()[match->variant.family].name, "ComplexEllipse");
  EXPECT_EQ(failure, PermutationMatchFailure::None);
}

TGFX_TEST(ShaderPermutationTest, ShapeInstancedTextureCoverageRegistry) {
  // The family is declaration-driven now: its domain spans the geometry's VERTEX_COLOR, the
  // Gradient module's PRESENT and Xfer's HAS_XP (2 * 2 * 3); the product rule prunes the
  // enumerated bundle to the 2 real programs.
  const auto* registered = FindShaderInfo("ShapeInstancedTextureCoverage");
  ASSERT_NE(registered, nullptr);
  EXPECT_EQ(registered->vertDomain.totalCount(), 12u);
  EXPECT_EQ(registered->fragDomain.totalCount(), 12u);
  EXPECT_TRUE(registered->vertexFile.empty());
  EXPECT_TRUE(registered->fragmentFile.empty());
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

static void ExpectDirectAARectMatch(ProgramInfo* programInfo,
                                    std::vector<const FragmentProcessor*> color,
                                    std::vector<const FragmentProcessor*> coverage,
                                    const char* shaderName) {
  // The matcher reads the draw's lowering graph through the carried IR; a hand-built ProgramInfo
  // carries none, and the families reject an IR-less shape outright. Lower the same processors
  // the way StandardDrawOp would and attach the result before matching.
  BlockAllocator allocator;
  auto lowered = LowerDraw(color, coverage, BlendMode::SrcOver, &allocator);
  ASSERT_TRUE(lowered.has_value());
  programInfo->setCarriedIR(&*lowered);
  auto match = SelectProgram(programInfo->getContext(), *programInfo, SelectionScope::AnyFamily);
  ASSERT_TRUE(match.has_value());
  EXPECT_EQ(Families()[match->variant.family].name, shaderName);
}

TGFX_TEST(ShaderPermutationTest, DirectAARectCoverageMatchesFiveLevel1Families) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  if (context->backend() == Backend::OpenGL) {
    GTEST_SKIP() << "Direct AARect families are outside the OpenGL stage 1 whitelist";
  }
  auto renderTargetProxy = RenderTargetProxy::Make(context, 8, 8, false);
  ASSERT_NE(renderTargetProxy, nullptr);
  auto renderTarget = renderTargetProxy->getRenderTarget();
  ASSERT_NE(renderTarget, nullptr);

  for (int xpType : {0, 1, 2}) {
    {
      BlockAllocator allocator;
      auto gp = QuadGeometryLayoutOf(8, 8, AAType::Coverage, PMColor::White(), Matrix::I(), false);
      auto clip = RectEffect::Make(&allocator, Rect::MakeWH(8, 8));
      auto dst = MakeSingleIntervalDst(context, xpType);
      ASSERT_NE(clip, nullptr);
      if (xpType != 0) {
        ASSERT_TRUE(dst.has_value());
      }
      ProgramInfo programInfo(renderTarget.get(), gp, dst, BlendMode::SrcOver, false);
      ExpectDirectAARectMatch(&programInfo, {}, {clip.get()}, "QuadColor");
    }
    {
      BlockAllocator allocator;
      auto textureProxy =
          context->proxyProvider()->createTextureProxy({}, 8, 8, PixelFormat::RGBA_8888);
      ASSERT_NE(textureProxy, nullptr);
      auto gp = QuadGeometryLayoutOf(8, 8, AAType::Coverage, PMColor::White(), Matrix::I(), false);
      auto color = TextureEffect::Make(&allocator, std::move(textureProxy));
      auto clip = RectEffect::Make(&allocator, Rect::MakeWH(8, 8));
      auto dst = MakeSingleIntervalDst(context, xpType);
      ASSERT_NE(color, nullptr);
      ASSERT_NE(clip, nullptr);
      if (xpType != 0) {
        ASSERT_TRUE(dst.has_value());
      }
      ProgramInfo programInfo(renderTarget.get(), gp, dst, BlendMode::SrcOver, false);
      ExpectDirectAARectMatch(&programInfo, {color.get()}, {clip.get()}, "QuadTexture");
    }
    {
      BlockAllocator allocator;
      auto gp = DefaultGeometryLayout(PMColor::White(), AAType::Coverage, Matrix::I(), Matrix::I());
      auto clip = RectEffect::Make(&allocator, Rect::MakeWH(8, 8));
      auto dst = MakeSingleIntervalDst(context, xpType);
      ASSERT_NE(clip, nullptr);
      if (xpType != 0) {
        ASSERT_TRUE(dst.has_value());
      }
      ProgramInfo programInfo(renderTarget.get(), gp, dst, BlendMode::SrcOver, false);
      ExpectDirectAARectMatch(&programInfo, {}, {clip.get()}, "SolidColor");
    }
    {
      BlockAllocator allocator;
      auto gp = HairlineLineGeometryLayout(PMColor::White(), Matrix::I(), Matrix::I(), 1.0f,
                                           AAType::Coverage);
      auto clip = RectEffect::Make(&allocator, Rect::MakeWH(8, 8));
      auto dst = MakeSingleIntervalDst(context, xpType);
      ASSERT_NE(clip, nullptr);
      if (xpType != 0) {
        ASSERT_TRUE(dst.has_value());
      }
      ProgramInfo programInfo(renderTarget.get(), gp, dst, BlendMode::SrcOver, false);
      ExpectDirectAARectMatch(&programInfo, {}, {clip.get()}, "HairlineLine");
    }
    {
      BlockAllocator allocator;
      auto gp = HairlineQuadGeometryLayout(PMColor::White(), Matrix::I(), Matrix::I(), 1.0f,
                                           AAType::Coverage);
      auto clip = RectEffect::Make(&allocator, Rect::MakeWH(8, 8));
      auto dst = MakeSingleIntervalDst(context, xpType);
      ASSERT_NE(clip, nullptr);
      if (xpType != 0) {
        ASSERT_TRUE(dst.has_value());
      }
      ProgramInfo programInfo(renderTarget.get(), gp, dst, BlendMode::SrcOver, false);
      ExpectDirectAARectMatch(&programInfo, {}, {clip.get()}, "HairlineQuad");
    }
  }
}

TGFX_TEST(ShaderPermutationTest, DirectAARectPreservesPermutationDomains) {
  struct ExpectedDomain {
    const char* name;
    uint32_t vertexCount;
    uint32_t fragmentCount;
  };
  const ExpectedDomain expected[] = {
      {"QuadColor", 6, 6},    {"QuadTexture", 48, 48}, {"SolidColor", 3, 3},
      {"HairlineLine", 3, 3}, {"HairlineQuad", 3, 3},
  };
  for (const auto& expectedDomain : expected) {
    // The lookup goes through Find so the declaration-driven families (SolidColor) are covered
    // alongside the factory families.
    const auto* registered = FindShaderInfo(expectedDomain.name);
    EXPECT_NE(registered, nullptr) << expectedDomain.name;
    if (registered == nullptr) {
      continue;
    }
    EXPECT_EQ(registered->vertDomain.totalCount(), expectedDomain.vertexCount);
    EXPECT_EQ(registered->fragDomain.totalCount(), expectedDomain.fragmentCount);
  }
}

// A Compose(mask, rect) coverage is the chain's documented narrow form (the mask child plus a clip
// slot), so the interpreter serves it: the shape is no longer a NoMatchingRule miss.
TGFX_TEST(ShaderPermutationTest, DirectAARectComposedCoverageIsServedByChain) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto renderTargetProxy = RenderTargetProxy::Make(context, 8, 8, false);
  ASSERT_NE(renderTargetProxy, nullptr);
  BlockAllocator allocator;
  auto gp = DefaultGeometryLayout(PMColor::White(), AAType::None, Matrix::I(), Matrix::I());
  auto maskProxy = context->proxyProvider()->createTextureProxy({}, 8, 8, PixelFormat::ALPHA_8);
  ASSERT_NE(maskProxy, nullptr);
  auto mask = DeviceSpaceTextureEffect::Make(&allocator, std::move(maskProxy), Matrix::I());
  auto clip = RectEffect::Make(&allocator, Rect::MakeWH(8, 8));
  auto composed = FragmentProcessor::Compose(&allocator, std::move(mask), std::move(clip));
  ASSERT_NE(composed, nullptr);
  // The chain serves the draw from its record-time IR: coverage is classified off its terms.
  auto ir = LowerDraw({}, {composed.get()}, BlendMode::SrcOver, &allocator);
  ASSERT_TRUE(ir.has_value());
  ProgramInfo programInfo(renderTargetProxy->getRenderTarget().get(), gp, std::nullopt,
                          BlendMode::SrcOver, false);
  programInfo.setCarriedIR(&*ir);
  auto failure = PermutationMatchFailure::None;
  auto served = SelectProgram(context, programInfo, SelectionScope::AnyFamily, &failure);
  ASSERT_TRUE(served.has_value());
  EXPECT_STREQ(Families()[served->variant.family].name, "Chain");
}

// --- MirroredDim: permanent desync guard --------------------------------------------------------
// A shader whose vertex and fragment stages share a dimension name gates a varying that both stages
// must declare identically; the framework's MirroredDimsAgree (ShaderPermutation) enforces value
// equality automatically, replacing the per-shader hand-written guards. For that automatic rule to
// be sound, a shared-name dimension must have the SAME arity (value count) in both domains --
// otherwise the two stages disagree on how many varying configurations exist and could emit a
// mismatched vertex/fragment interface. This test asserts that structural invariant for every
// registered shader, turning a future "added the dim to one stage / with a different arity" mistake
// into a build-time failure instead of an invalid runtime pipeline.
static const char* MirrorDimName(const PermutationDimension& dimension) {
  return std::visit([](const auto& d) { return d.defineName; }, dimension);
}

static int MirrorDimArity(const PermutationDimension& dimension) {
  return std::visit([](const auto& d) { return d.valueCount(); }, dimension);
}

TGFX_TEST(ShaderPermutationTest, MirroredDimsHaveConsistentArity) {
  for (size_t index = 0; index < FamilyCount(); ++index) {
    const auto info = AssembledFamilyInfo(Families()[index]);
    const auto& vertDims = info.vertDomain.getDimensions();
    const auto& fragDims = info.fragDomain.getDimensions();
    for (const auto& fragDim : fragDims) {
      std::string fragName = MirrorDimName(fragDim);
      for (const auto& vertDim : vertDims) {
        if (std::string(MirrorDimName(vertDim)) == fragName) {
          EXPECT_EQ(MirrorDimArity(vertDim), MirrorDimArity(fragDim))
              << info.name << " mirror dimension '" << fragName
              << "' has differing arity between the vertex and fragment domains";
        }
      }
    }
  }
}

}  // namespace tgfx

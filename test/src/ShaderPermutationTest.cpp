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
#include <fstream>
#include <set>
#include <vector>
#include "base/TGFXTest.h"
#include "core/filters/GaussianBlurImageFilter.h"
#include "core/utils/BlockAllocator.h"
#include "gpu/EmbeddedShaderBundles.h"
#include "gpu/GlobalCache.h"
#include "gpu/PermutationMatcher.h"
#include "gpu/PrecompiledShaderCache.h"
#include "gpu/ProxyProvider.h"
#include "gpu/processors/AARectEffect.h"
#include "gpu/processors/AlphaThresholdFragmentProcessor.h"
#include "gpu/processors/ClampedGradientEffect.h"
#include "gpu/processors/ColorMatrixFragmentProcessor.h"
#include "gpu/processors/ConstColorProcessor.h"
#include "gpu/processors/DefaultGeometryProcessor.h"
#include "gpu/processors/DeviceSpaceTextureEffect.h"
#include "gpu/processors/HairlineLineGeometryProcessor.h"
#include "gpu/processors/HairlineQuadGeometryProcessor.h"
#include "gpu/processors/LumaFragmentProcessor.h"
#include "gpu/processors/MeshGeometryProcessor.h"
#include "gpu/processors/PorterDuffXferProcessor.h"
#include "gpu/processors/QuadPerEdgeAAGeometryProcessor.h"
#include "gpu/processors/RadialGradientLayout.h"
#include "gpu/processors/ShapeInstancedGeometryProcessor.h"
#include "gpu/processors/SingleIntervalGradientColorizer.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/processors/TiledTextureEffect.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "gpu/shaders/PrecompiledShader.h"
#include "gpu/shaders/ShaderPermutation.h"
#include "gpu/shaders/level1/DeviceSpaceTexturedEffectShader.h"
#include "gpu/shaders/level1/QuadConstColorShader.h"
#include "gpu/shaders/level1/QuadTextureFillShader.h"
#include "gpu/shaders/level1/ShapeInstancedFillShader.h"
#include "gpu/shaders/level1/ShapeInstancedTextureCoverageShader.h"
#include "gpu/shaders/level1/TextureFillShader.h"
#include "gpu/shaders/level1/UnifiedGradientShader.h"
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

static constexpr std::array<float, 20> MatcherIdentityColorMatrix = {
    1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0,
};

static PlacementPtr<FragmentProcessor> MakeDeviceSpacePointwiseProcessor(Context* context,
                                                                         BlockAllocator* allocator,
                                                                         PixelFormat format,
                                                                         bool useLuma) {
  auto textureProxy = context->proxyProvider()->createTextureProxy({}, 2, 2, format);
  if (textureProxy == nullptr || textureProxy->getTextureView() == nullptr) {
    return nullptr;
  }
  auto source = DeviceSpaceTextureEffect::Make(allocator, std::move(textureProxy), Matrix::I());
  if (source == nullptr) {
    return nullptr;
  }
  if (useLuma) {
    auto pointwise = LumaFragmentProcessor::Make(allocator);
    return FragmentProcessor::Compose(allocator, std::move(source), std::move(pointwise));
  }
  auto pointwise = ColorMatrixFragmentProcessor::Make(allocator, MatcherIdentityColorMatrix);
  return FragmentProcessor::Compose(allocator, std::move(source), std::move(pointwise));
}

static PlacementPtr<GeometryProcessor> MakeSingleIntervalGeometry(BlockAllocator* allocator,
                                                                  int gpType,
                                                                  bool hasVertexCoverage) {
  auto aa = hasVertexCoverage ? AAType::Coverage : AAType::None;
  if (gpType == 0) {
    return DefaultGeometryProcessor::Make(allocator, PMColor::White(), 8, 8, aa, Matrix::I(),
                                          Matrix::I());
  }
  return QuadPerEdgeAAGeometryProcessor::Make(allocator, 8, 8, aa, PMColor::White(), Matrix::I(),
                                              false);
}

static PlacementPtr<FragmentProcessor> MakeSingleIntervalGradient(BlockAllocator* allocator) {
  auto colorizer = SingleIntervalGradientColorizer::Make(allocator, Color::Red(), Color::Blue());
  auto layout = RadialGradientLayout::Make(allocator, Matrix::I());
  return ClampedGradientEffect::Make(allocator, std::move(colorizer), std::move(layout),
                                     Color::Red(), Color::Blue());
}

static PlacementPtr<FragmentProcessor> MakeSingleIntervalCoverage(Context* context,
                                                                  BlockAllocator* allocator,
                                                                  int coverageType) {
  if (coverageType == 0) {
    return nullptr;
  }
  if (coverageType == 1) {
    return AARectEffect::Make(allocator, Rect::MakeWH(8, 8));
  }
  auto proxy = context->proxyProvider()->createTextureProxy({}, 8, 8, PixelFormat::ALPHA_8);
  if (proxy == nullptr || proxy->getTextureView() == nullptr) {
    return nullptr;
  }
  return DeviceSpaceTextureEffect::Make(allocator, std::move(proxy), Matrix::I());
}

static PlacementPtr<XferProcessor> MakeSingleIntervalXP(Context* context, BlockAllocator* allocator,
                                                        int xpType) {
  if (xpType == 0) {
    return nullptr;
  }
  DstTextureInfo dstTextureInfo = {};
  if (xpType == 1) {
    auto proxy = context->proxyProvider()->createTextureProxy({}, 8, 8, PixelFormat::RGBA_8888);
    if (proxy == nullptr || proxy->getTextureView() == nullptr) {
      return nullptr;
    }
    dstTextureInfo = {std::move(proxy), {}};
  }
  return PorterDuffXferProcessor::Make(allocator, BlendMode::SrcOver, std::move(dstTextureInfo));
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
  using D = ShapeInstancedFillShader::Dims;
  EXPECT_EQ(D::COUNT, 2u);
  EXPECT_EQ(D::HAS_COLOR, 0u);
  EXPECT_EQ(D::HAS_AA, 1u);

  auto domain = D::domain();
  EXPECT_EQ(domain.dimensionCount(), static_cast<size_t>(D::COUNT));
  EXPECT_EQ(domain.totalCount(), 4u);

  // Verify domain dimensions match.
  auto handDomain = PermutationDomain::FromBoolNames("HAS_COLOR, HAS_AA");
  EXPECT_EQ(handDomain.totalCount(), domain.totalCount());
  EXPECT_EQ(handDomain.dimensionCount(), domain.dimensionCount());

  // Verify encode/decode match between the two.
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
    if (shaderInfo.name == TextureFillShader::Name()) {
      foundTextureFill = true;
      EXPECT_EQ(shaderInfo.vertDomain.totalCount(), 1u);
      EXPECT_EQ(shaderInfo.vertDomain.dimensionCount(), 0u);
      // FragDims: HAS_XP(int3) + HAS_DEVICE_MASK(bool) = 6 permutations. AARect clipping is a
      // runtime uniform (Rect / HasClip), not a compile-time dimension.
      EXPECT_EQ(shaderInfo.fragDomain.totalCount(), 6u);
      EXPECT_EQ(shaderInfo.fragDomain.dimensionCount(), 2u);
      EXPECT_EQ(shaderInfo.vertexFile, "level1/texture_fill.vert");
      EXPECT_EQ(shaderInfo.fragmentFile, "level1/texture_fill.frag");
    }
  }
  EXPECT_TRUE(foundTextureFill);
}

TGFX_TEST(ShaderPermutationTest, DeviceSpaceTexturedEffectShaderRegistry) {
  bool found = false;
  for (const auto& factory : ShaderRegistry::All()) {
    auto info = factory()->info();
    if (info.name != "DeviceSpaceTexturedEffectShader") {
      continue;
    }
    found = true;
    EXPECT_EQ(info.vertexFile, "level1/device_space_texture.vert");
    EXPECT_EQ(info.fragmentFile, "level1/device_space_textured_effect.frag");
    EXPECT_EQ(info.vertDomain.totalCount(), 2u);
    EXPECT_EQ(info.fragDomain.totalCount(), 6u);
    int compiledCount = 0;
    for (uint32_t vi = 0; vi < info.vertDomain.totalCount(); ++vi) {
      auto vertValues = info.vertDomain.decode(vi);
      for (uint32_t fi = 0; fi < info.fragDomain.totalCount(); ++fi) {
        auto fragValues = info.fragDomain.decode(fi);
        if (MirroredDimsAgree(info.vertDomain, info.fragDomain, vertValues, fragValues)) {
          compiledCount++;
        }
      }
    }
    EXPECT_EQ(compiledCount, 6);
  }
  EXPECT_TRUE(found);
}

TGFX_TEST(ShaderPermutationTest, DeviceSpaceTexturedEffectMatchesSupportedLayouts) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  if (context->backend() == Backend::OpenGL) {
    GTEST_SKIP() << "OpenGL stage 1 validates only the pixel-crossed pointwise layouts";
  }
  auto renderTargetProxy = RenderTargetProxy::Make(context, 8, 8, false);
  ASSERT_NE(renderTargetProxy, nullptr);
  auto renderTarget = renderTargetProxy->getRenderTarget();
  ASSERT_NE(renderTarget, nullptr);

  {
    BlockAllocator allocator;
    auto gp = DefaultGeometryProcessor::Make(&allocator, PMColor::White(), 8, 8, AAType::None,
                                             Matrix::I(), Matrix::I());
    auto composed =
        MakeDeviceSpacePointwiseProcessor(context, &allocator, PixelFormat::RGBA_8888, false);
    ASSERT_NE(gp, nullptr);
    ASSERT_NE(composed, nullptr);
    ProgramInfo programInfo(renderTarget.get(), gp.get(), {composed.get()}, 1, nullptr,
                            BlendMode::SrcOver);
    auto match = MatchPermutation(&programInfo);
    ASSERT_TRUE(match.has_value());
    EXPECT_EQ(match->shaderName, "DeviceSpaceTexturedEffectShader");
    EXPECT_EQ(match->vertPermutationIndex, 0u);
    EXPECT_EQ(match->fragPermutationIndex, 0u);
  }

  {
    BlockAllocator allocator;
    auto gp = QuadPerEdgeAAGeometryProcessor::Make(&allocator, 8, 8, AAType::Coverage,
                                                   PMColor::White(), Matrix::I(), false);
    auto composed =
        MakeDeviceSpacePointwiseProcessor(context, &allocator, PixelFormat::RGBA_8888, true);
    auto dstTexture =
        context->proxyProvider()->createTextureProxy({}, 8, 8, PixelFormat::RGBA_8888);
    ASSERT_NE(dstTexture, nullptr);
    ASSERT_NE(dstTexture->getTextureView(), nullptr);
    DstTextureInfo dstTextureInfo = {std::move(dstTexture), {}};
    auto xp =
        PorterDuffXferProcessor::Make(&allocator, BlendMode::SrcOver, std::move(dstTextureInfo));
    ASSERT_NE(gp, nullptr);
    ASSERT_NE(composed, nullptr);
    ASSERT_NE(xp, nullptr);
    ProgramInfo programInfo(renderTarget.get(), gp.get(), {composed.get()}, 1, xp.get(),
                            BlendMode::SrcOver);
    auto match = MatchPermutation(&programInfo);
    ASSERT_TRUE(match.has_value());
    EXPECT_EQ(match->shaderName, "DeviceSpaceTexturedEffectShader");
    EXPECT_EQ(match->vertPermutationIndex, 1u);
    EXPECT_EQ(match->fragPermutationIndex, 4u);
  }

  {
    BlockAllocator allocator;
    auto gp = DefaultGeometryProcessor::Make(&allocator, PMColor::White(), 8, 8, AAType::None,
                                             Matrix::I(), Matrix::I());
    auto composed =
        MakeDeviceSpacePointwiseProcessor(context, &allocator, PixelFormat::RGBA_8888, true);
    auto xp = PorterDuffXferProcessor::Make(&allocator, BlendMode::SrcOver, {});
    ASSERT_NE(gp, nullptr);
    ASSERT_NE(composed, nullptr);
    ASSERT_NE(xp, nullptr);
    ProgramInfo programInfo(renderTarget.get(), gp.get(), {composed.get()}, 1, xp.get(),
                            BlendMode::SrcOver);
    auto match = MatchPermutation(&programInfo);
    if (programInfo.backend() == Backend::OpenGL) {
      EXPECT_FALSE(match.has_value());
    } else {
      ASSERT_TRUE(match.has_value());
      EXPECT_EQ(match->fragPermutationIndex, 2u);
    }
  }
}

TGFX_TEST(ShaderPermutationTest, DeviceSpaceTexturedEffectRejectsUnsupportedLayouts) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto renderTargetProxy = RenderTargetProxy::Make(context, 8, 8, false);
  ASSERT_NE(renderTargetProxy, nullptr);
  auto renderTarget = renderTargetProxy->getRenderTarget();
  ASSERT_NE(renderTarget, nullptr);

  {
    BlockAllocator allocator;
    auto gp = DefaultGeometryProcessor::Make(&allocator, PMColor::White(), 8, 8, AAType::None,
                                             Matrix::I(), Matrix::I());
    auto composed =
        MakeDeviceSpacePointwiseProcessor(context, &allocator, PixelFormat::ALPHA_8, false);
    ASSERT_NE(gp, nullptr);
    ASSERT_NE(composed, nullptr);
    ProgramInfo programInfo(renderTarget.get(), gp.get(), {composed.get()}, 1, nullptr,
                            BlendMode::SrcOver);
    auto match = MatchPermutation(&programInfo);
    if (programInfo.backend() == Backend::OpenGL) {
      EXPECT_FALSE(match.has_value());
    } else {
      // Alpha-only sources are served by the kernel's AlphaOnly runtime uniform.
      EXPECT_TRUE(match.has_value());
    }
  }

  {
    BlockAllocator allocator;
    auto gp = QuadPerEdgeAAGeometryProcessor::Make(&allocator, 8, 8, AAType::None, std::nullopt,
                                                   Matrix::I(), false);
    auto composed =
        MakeDeviceSpacePointwiseProcessor(context, &allocator, PixelFormat::RGBA_8888, false);
    ASSERT_NE(gp, nullptr);
    ASSERT_NE(composed, nullptr);
    ProgramInfo programInfo(renderTarget.get(), gp.get(), {composed.get()}, 1, nullptr,
                            BlendMode::SrcOver);
    EXPECT_FALSE(MatchPermutation(&programInfo).has_value());
  }

  {
    BlockAllocator allocator;
    auto gp = DefaultGeometryProcessor::Make(&allocator, PMColor::White(), 8, 8, AAType::None,
                                             Matrix::I(), Matrix::I());
    auto composed =
        MakeDeviceSpacePointwiseProcessor(context, &allocator, PixelFormat::RGBA_8888, false);
    auto coverage = AARectEffect::Make(&allocator, Rect::MakeWH(8, 8));
    ASSERT_NE(gp, nullptr);
    ASSERT_NE(composed, nullptr);
    ASSERT_NE(coverage, nullptr);
    ProgramInfo programInfo(renderTarget.get(), gp.get(), {composed.get(), coverage.get()}, 1,
                            nullptr, BlendMode::SrcOver);
    EXPECT_FALSE(MatchPermutation(&programInfo).has_value());
  }

  {
    BlockAllocator allocator;
    auto gp = DefaultGeometryProcessor::Make(&allocator, PMColor::White(), 8, 8, AAType::None,
                                             Matrix::I(), Matrix::I());
    auto textureProxy =
        context->proxyProvider()->createTextureProxy({}, 2, 2, PixelFormat::RGBA_8888);
    auto source = DeviceSpaceTextureEffect::Make(&allocator, std::move(textureProxy), Matrix::I());
    auto pointwise = AlphaThresholdFragmentProcessor::Make(&allocator, 0.5f);
    auto composed = FragmentProcessor::Compose(&allocator, std::move(source), std::move(pointwise));
    ASSERT_NE(gp, nullptr);
    ASSERT_NE(composed, nullptr);
    ProgramInfo programInfo(renderTarget.get(), gp.get(), {composed.get()}, 1, nullptr,
                            BlendMode::SrcOver);
    EXPECT_FALSE(MatchPermutation(&programInfo).has_value());
  }

  {
    BlockAllocator allocator;
    auto gp = DefaultGeometryProcessor::Make(&allocator, PMColor::White(), 8, 8, AAType::None,
                                             Matrix::I(), Matrix::I());
    auto textureProxy =
        context->proxyProvider()->createTextureProxy({}, 2, 2, PixelFormat::RGBA_8888);
    auto source = DeviceSpaceTextureEffect::Make(&allocator, std::move(textureProxy), Matrix::I());
    auto pointwise = LumaFragmentProcessor::Make(&allocator);
    ASSERT_NE(gp, nullptr);
    ASSERT_NE(source, nullptr);
    ASSERT_NE(pointwise, nullptr);
    ProgramInfo programInfo(renderTarget.get(), gp.get(), {source.get(), pointwise.get()}, 2,
                            nullptr, BlendMode::SrcOver);
    EXPECT_FALSE(MatchPermutation(&programInfo).has_value());
  }
}

TGFX_TEST(ShaderPermutationTest, SingleIntervalGradientCompiledSpace) {
  UnifiedGradientShader shader;
  auto info = shader.info();
  EXPECT_EQ(info.vertDomain.totalCount(), 2u);
  EXPECT_EQ(info.fragDomain.totalCount(), 12u);

  std::set<uint32_t> vertexIndices;
  std::set<uint32_t> fragmentIndices;
  uint32_t compiledCount = 0;
  for (uint32_t vi = 0; vi < info.vertDomain.totalCount(); ++vi) {
    for (uint32_t fi = 0; fi < info.fragDomain.totalCount(); ++fi) {
      if (!IsBuildablePermutation(info, vi, fi)) {
        continue;
      }
      compiledCount++;
      vertexIndices.insert(vi);
      fragmentIndices.insert(fi);
    }
  }
  // 12 raw frag combos minus the three LUT-with-coverage combos excluded by ShouldCompile; the
  // device mask is a runtime uniform now, not a dimension.
  EXPECT_EQ(compiledCount, 9u);
  EXPECT_EQ(vertexIndices.size(), 2u);
  EXPECT_EQ(fragmentIndices.size(), 9u);
  EXPECT_TRUE(IsBuildablePermutation(info, 1, 5));
  EXPECT_FALSE(IsBuildablePermutation(info, 0, 5));
}

TGFX_TEST(ShaderPermutationTest, SingleIntervalGradientMatcherSpace) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  if (context->backend() == Backend::OpenGL) {
    GTEST_SKIP() << "SingleIntervalGradient is outside the OpenGL stage 1 whitelist";
  }
  auto renderTargetProxy = RenderTargetProxy::Make(context, 8, 8, false);
  ASSERT_NE(renderTargetProxy, nullptr);
  auto renderTarget = renderTargetProxy->getRenderTarget();
  ASSERT_NE(renderTarget, nullptr);

  UnifiedGradientShader shader;
  auto info = shader.info();
  std::set<uint32_t> vertexIndices;
  std::set<uint32_t> fragmentIndices;
  std::set<uint64_t> pairs;
  for (int gpType = 0; gpType < 2; ++gpType) {
    for (int hasVertexCoverage = 0; hasVertexCoverage < 2; ++hasVertexCoverage) {
      for (int xpType = 0; xpType < 3; ++xpType) {
        for (int coverageType = 0; coverageType < 3; ++coverageType) {
          BlockAllocator allocator;
          auto gp = MakeSingleIntervalGeometry(&allocator, gpType, hasVertexCoverage != 0);
          auto gradient = MakeSingleIntervalGradient(&allocator);
          auto coverage = MakeSingleIntervalCoverage(context, &allocator, coverageType);
          auto xp = MakeSingleIntervalXP(context, &allocator, xpType);
          ASSERT_NE(gp, nullptr);
          ASSERT_NE(gradient, nullptr);
          if (coverageType != 0) {
            ASSERT_NE(coverage, nullptr);
          }
          if (xpType != 0) {
            ASSERT_NE(xp, nullptr);
          }
          std::vector<FragmentProcessor*> processors = {gradient.get()};
          if (coverage != nullptr) {
            processors.push_back(coverage.get());
          }
          ProgramInfo programInfo(renderTarget.get(), gp.get(), std::move(processors), 1, xp.get(),
                                  BlendMode::SrcOver);
          auto match = MatchPermutation(&programInfo);
          ASSERT_TRUE(match.has_value());
          EXPECT_EQ(match->shaderName, "UnifiedGradientShader");
          ASSERT_LT(match->vertPermutationIndex, info.vertDomain.totalCount());
          ASSERT_LT(match->fragPermutationIndex, info.fragDomain.totalCount());
          auto vertValues = info.vertDomain.decode(match->vertPermutationIndex);
          auto fragValues = info.fragDomain.decode(match->fragPermutationIndex);
          EXPECT_TRUE(MirroredDimsAgree(info.vertDomain, info.fragDomain, vertValues, fragValues));
          EXPECT_TRUE(!info.shouldCompile ||
                      info.shouldCompile(match->vertPermutationIndex, match->fragPermutationIndex,
                                         vertValues, fragValues));
          vertexIndices.insert(match->vertPermutationIndex);
          fragmentIndices.insert(match->fragPermutationIndex);
          pairs.insert((static_cast<uint64_t>(match->vertPermutationIndex) << 32u) |
                       match->fragPermutationIndex);
        }
      }
    }
  }
  EXPECT_EQ(vertexIndices.size(), 2u);
  // The device mask no longer contributes a dimension, so only XP and coverage vary.
  EXPECT_EQ(fragmentIndices.size(), 6u);
  EXPECT_EQ(pairs.size(), 6u);

  for (int hasVertexCoverage = 0; hasVertexCoverage < 2; ++hasVertexCoverage) {
    BlockAllocator allocator;
    auto gp = MakeSingleIntervalGeometry(&allocator, 1, hasVertexCoverage != 0);
    auto gradient = MakeSingleIntervalGradient(&allocator);
    auto maskProxy = context->proxyProvider()->createTextureProxy({}, 8, 8, PixelFormat::RGBA_8888);
    ASSERT_NE(gp, nullptr);
    ASSERT_NE(gradient, nullptr);
    ASSERT_NE(maskProxy, nullptr);
    ASSERT_NE(maskProxy->getTextureView(), nullptr);
    SamplingArgs sampling(TileMode::Repeat, TileMode::Repeat, SamplingOptions(),
                          SrcRectConstraint::Fast);
    auto tiledMask = TiledTextureEffect::Make(&allocator, std::move(maskProxy), sampling);
    ASSERT_NE(tiledMask, nullptr);
    ASSERT_EQ(tiledMask->name(), "TiledTextureEffect");
    int modeX = -1;
    int modeY = -1;
    static_cast<TiledTextureEffect*>(tiledMask.get())->getShaderModes(&modeX, &modeY);
    ASSERT_EQ(modeX, 0);
    ASSERT_EQ(modeY, 0);
    auto coverage =
        FragmentProcessor::MulInputByChildAlpha(&allocator, std::move(tiledMask), false);
    ASSERT_NE(coverage, nullptr);
    ProgramInfo programInfo(renderTarget.get(), gp.get(), {gradient.get(), coverage.get()}, 1,
                            nullptr, BlendMode::SrcOver);
    EXPECT_FALSE(MatchPermutation(&programInfo).has_value());
  }
}

TGFX_TEST(ShaderPermutationTest, QuadTextureLocalMaskRejectsInvertedCoverage) {
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
    auto gp = QuadPerEdgeAAGeometryProcessor::Make(&allocator, 8, 8, AAType::None, PMColor::White(),
                                                   Matrix::I(), false);
    auto colorProxy =
        context->proxyProvider()->createTextureProxy({}, 8, 8, PixelFormat::RGBA_8888);
    auto maskProxy = context->proxyProvider()->createTextureProxy({}, 8, 8, PixelFormat::RGBA_8888);
    auto color = TextureEffect::Make(&allocator, std::move(colorProxy));
    auto mask = TextureEffect::Make(&allocator, std::move(maskProxy));
    auto coverage = FragmentProcessor::MulInputByChildAlpha(&allocator, std::move(mask), inverted);
    ASSERT_NE(gp, nullptr);
    ASSERT_NE(color, nullptr);
    ASSERT_NE(coverage, nullptr);
    ProgramInfo programInfo(renderTarget.get(), gp.get(), {color.get(), coverage.get()}, 1, nullptr,
                            BlendMode::SrcOver);
    auto match = MatchPermutation(&programInfo);
    if (inverted) {
      EXPECT_FALSE(match.has_value());
    } else {
      ASSERT_TRUE(match.has_value());
      EXPECT_EQ(match->shaderName, "QuadTextureFillShader");
    }
  }
}

TGFX_TEST(ShaderPermutationTest, ShouldCompile) {
  auto& factories = ShaderRegistry::All();
  for (auto& factory : factories) {
    auto shader = factory();
    auto shaderInfo = shader->info();
    if (shaderInfo.name != TextureFillShader::Name()) {
      continue;
    }
    // Vert: no dimensions = 1 raw. Frag: HAS_XP(int3) + HAS_DEVICE_MASK(bool) = 6 raw. YUV is
    // rejected by the matcher before matching and subset clamping is a runtime uniform, so every
    // enumerated combination is buildable: 1 * 6 = 6.
    int compiledCount = 0;
    for (uint32_t vi = 0; vi < shaderInfo.vertDomain.totalCount(); vi++) {
      auto vertValues = shaderInfo.vertDomain.decode(vi);
      for (uint32_t fi = 0; fi < shaderInfo.fragDomain.totalCount(); fi++) {
        auto fragValues = shaderInfo.fragDomain.decode(fi);
        // Mirror the production enumeration: framework mirror rule first, then the shader rule.
        if (!MirroredDimsAgree(shaderInfo.vertDomain, shaderInfo.fragDomain, vertValues,
                               fragValues)) {
          continue;
        }
        if (!shaderInfo.shouldCompile || shaderInfo.shouldCompile(vi, fi, vertValues, fragValues)) {
          compiledCount++;
        }
      }
    }
    EXPECT_EQ(compiledCount, 6);
  }
}

TGFX_TEST(ShaderPermutationTest, TextureFillVertexDomainIsInvariant) {
  auto domain = TextureFillShader::VD::domain();
  EXPECT_EQ(domain.dimensionCount(), 0u);
  EXPECT_EQ(domain.totalCount(), 1u);
  EXPECT_EQ(TextureFillShader::EncodeVertex(), 0u);
}

TGFX_TEST(ShaderPermutationTest, TextureFillTypedEncodingMatchesDomains) {
  auto vertexDomain = TextureFillShader::VD::domain();
  EXPECT_EQ(vertexDomain.totalCount(), 1u);
  EXPECT_EQ(TextureFillShader::EncodeVertex(), 0u);

  auto fragmentDomain = TextureFillShader::FD::domain();
  for (uint32_t index = 0; index < fragmentDomain.totalCount(); index++) {
    auto values = fragmentDomain.decode(index);
    TextureFillShader::FragmentValues typedValues = {};
    typedValues.xp = static_cast<uint32_t>(values[TextureFillShader::FD::HAS_XP]);
    typedValues.deviceMask = static_cast<uint32_t>(values[TextureFillShader::FD::HAS_DEVICE_MASK]);
    EXPECT_EQ(TextureFillShader::EncodeFragment(typedValues), index);
  }
}

TGFX_TEST(ShaderPermutationTest, PrecompiledBundleLoad) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto bundlePath = ProjectPath::Absolute(BundlePath());
  auto* cache = context->precompiledShaderCache();
  ASSERT_TRUE(cache->loadBundle(bundlePath));
  EXPECT_EQ(cache->vertexEntryCount(), 96u);
  EXPECT_EQ(cache->fragmentEntryCount(), 271u);
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
    context->precompiledShaderCache()->unload();
    ScopedAOTStatsPause statsPause(context, true);
    context->globalCache()->clearPrograms();
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
    context->globalCache()->clearPrograms();
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
    context->globalCache()->clearPrograms();
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
    ScopedAOTStatsPause statsPause(context, true);
    context->globalCache()->clearPrograms();
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
  hitRecord.vertPermutationIndex = 1;
  hitRecord.fragPermutationIndex = 2;
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

  cache.unload();
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
  auto* cache = context->precompiledShaderCache();
  cache->unload();
  image = MakeTexture2DImage(context, image);
  ASSERT_TRUE(image != nullptr);
  auto unrelatedBundle = MakeTestBundle("missing-artifacts", 1, 1, 10);
  ASSERT_TRUE(cache->loadBundle(unrelatedBundle.data(), unrelatedBundle.size()));
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
    EXPECT_FALSE(fallbackRecords[0].shaderName.empty());
    EXPECT_FALSE(fallbackRecords[0].effectSignature.empty());
    EXPECT_FALSE(fallbackRecords[0].pipelineSignature.empty());
  }
  const auto& programStats = context->globalCache()->programStats();
  EXPECT_EQ(programStats.cacheMisses, 1u);
  EXPECT_EQ(programStats.precompiledArtifactCreations, 0u);
  EXPECT_EQ(programStats.programBuilderCreations, 1u);

  cache->setDiagnosticRecordingEnabled(false);
  cache->unload();
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

  // Production bundles ship compressed; when the resource bundle is already compressed, the
  // load itself is the roundtrip coverage, so just verify entries and tag.
  if (TestReadU16LE(original.data() + 6) == 1u) {
    PrecompiledShaderCache compressedOnly;
    ASSERT_TRUE(compressedOnly.loadBundle(original.data(), original.size()));
    EXPECT_TRUE(compressedOnly.isLoaded());
    EXPECT_EQ(compressedOnly.vertexEntryCount(), 96u);
    EXPECT_EQ(compressedOnly.fragmentEntryCount(), 271u);
    std::string tag = TGFX_BACKEND_NAME;
    auto dash = tag.find('-');
    if (dash != std::string::npos) {
      tag = tag.substr(0, dash);
    }
    EXPECT_EQ(compressedOnly.profileTag(), tag);
    return;
  }

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
  if (context->backend() == Backend::OpenGL) {
    GTEST_SKIP() << "plain texture fill is outside the OpenGL stage 1 whitelist";
  }
  auto surface = Surface::Make(context, 200, 200);
  ASSERT_TRUE(surface != nullptr);
  auto bundlePath = ProjectPath::Absolute(BundlePath());
  auto* cache = context->precompiledShaderCache();
  cache->unload();
  image = MakeTexture2DImage(context, image);
  ASSERT_TRUE(image != nullptr);
  ASSERT_TRUE(cache->loadBundle(bundlePath));
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
  EXPECT_EQ(programStats.programBuilderCreations, 0u);

  cache->setDiagnosticRecordingEnabled(false);
  cache->unload();
}

TGFX_TEST(ShaderPermutationTest, AlphaThresholdHitsPrecompiledCache) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  if (context->backend() == Backend::OpenGL) {
    GTEST_SKIP() << "AlphaThreshold is outside the OpenGL stage 1 whitelist";
  }
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
  if (context->backend() == Backend::OpenGL) {
    GTEST_SKIP() << "standalone Luma is outside the OpenGL stage 1 whitelist";
  }
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
  if (context->backend() == Backend::OpenGL) {
    GTEST_SKIP() << "GaussianBlur is outside the OpenGL stage 1 whitelist";
  }
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

TGFX_TEST(ShaderPermutationTest, ChainBlendClipHitsPrecompiledCache) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  if (context->backend() == Backend::OpenGL) {
    GTEST_SKIP() << "The pointwise chain is outside the OpenGL stage 1 whitelist";
  }
  auto bundlePath = ProjectPath::Absolute(BundlePath());
  auto* cache = context->precompiledShaderCache();
  ASSERT_TRUE(cache->loadBundle(bundlePath));
  cache->resetStats();
  auto surface = Surface::Make(context, 200, 200);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();
  // Draw with ColorFilter::Blend (produces XfermodeFragmentProcessor) + AA clip rect
  // (produces AARectEffect as coverage FP). The pointwise chain serves this with an
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
  cache->unload();
}

TGFX_TEST(ShaderPermutationTest, MaskFillHitsPrecompiledCache) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  if (context->backend() == Backend::OpenGL) {
    GTEST_SKIP() << "MaskFill is outside the OpenGL stage 1 whitelist";
  }
  auto bundlePath = ProjectPath::Absolute(BundlePath());
  auto* cache = context->precompiledShaderCache();
  ASSERT_TRUE(cache->loadBundle(bundlePath));
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
    if (record.shaderName == "MaskFillShader") {
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
      // VertDims: HAS_UV_COORD, HAS_SUBSET, HAS_LOCAL_MASK. Coverage and color are unconditional
      // vertex attributes, so they no longer appear as dimensions.
      EXPECT_EQ(shaderInfo.vertDomain.dimensionCount(), 3u);
      EXPECT_EQ(shaderInfo.vertDomain.totalCount(), 8u);
      // FragDims: 2 bools + 1 int(3) = 2^2 * 3 = 12 total permutations. The device mask is a
      // runtime uniform; ALPHA_ONLY, HAS_RGBAAA and the unsupported YUV path are runtime/fallback
      // concerns; HAS_LOCAL_MASK is a mirror dimension.
      EXPECT_EQ(shaderInfo.fragDomain.dimensionCount(), 3u);
      EXPECT_EQ(shaderInfo.fragDomain.totalCount(), 12u);
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
        // Mirror the production enumeration: framework mirror rule first, then the shader rule.
        if (!MirroredDimsAgree(shaderInfo.vertDomain, shaderInfo.fragDomain, vertValues,
                               fragValues)) {
          continue;
        }
        if (!shaderInfo.shouldCompile || shaderInfo.shouldCompile(vi, fi, vertValues, fragValues)) {
          compiledCount++;
        }
      }
    }
    // ALPHA_ONLY / HAS_RGBAAA are runtime uniforms and per-vertex color/coverage are now
    // unconditional; the device mask is runtime too, leaving 24 variants.
    EXPECT_EQ(compiledCount, 24);
  }
}

TGFX_TEST(ShaderPermutationTest, SolidColorFillShouldCompile) {
  auto& factories = ShaderRegistry::All();
  bool found = false;
  for (auto& factory : factories) {
    auto shader = factory();
    auto shaderInfo = shader->info();
    if (shaderInfo.name != "SolidColorFillShader") {
      continue;
    }
    found = true;
    // Vert: HAS_COVERAGE(2). Frag: HAS_COVERAGE(2) x HAS_XP(3) = 12 raw. HAS_COVERAGE is a mirror
    // dimension enforced by the framework (MirroredDimsAgree), so only matching-HAS_COVERAGE pairs
    // compile -> 2 * 3 = 6.
    EXPECT_EQ(shaderInfo.vertDomain.totalCount(), 2u);
    EXPECT_EQ(shaderInfo.fragDomain.totalCount(), 6u);
    int compiledCount = 0;
    for (uint32_t vi = 0; vi < shaderInfo.vertDomain.totalCount(); vi++) {
      auto vertValues = shaderInfo.vertDomain.decode(vi);
      for (uint32_t fi = 0; fi < shaderInfo.fragDomain.totalCount(); fi++) {
        auto fragValues = shaderInfo.fragDomain.decode(fi);
        // Mirror the production enumeration: framework mirror rule first, then any shader rule.
        if (!MirroredDimsAgree(shaderInfo.vertDomain, shaderInfo.fragDomain, vertValues,
                               fragValues)) {
          continue;
        }
        if (!shaderInfo.shouldCompile || shaderInfo.shouldCompile(vi, fi, vertValues, fragValues)) {
          compiledCount++;
        }
      }
    }
    EXPECT_EQ(compiledCount, 6);
  }
  EXPECT_TRUE(found);
}

TGFX_TEST(ShaderPermutationTest, MaskFillShouldCompile) {
  auto& factories = ShaderRegistry::All();
  bool found = false;
  for (auto& factory : factories) {
    auto shader = factory();
    auto shaderInfo = shader->info();
    if (shaderInfo.name != "MaskFillShader") {
      continue;
    }
    found = true;
    // No vertex dimensions; the fragment stage carries a single HAS_XP dimension (3 values:
    // none / PorterDuff DST_TEX / PorterDuff framebuffer-fetch).
    EXPECT_EQ(shaderInfo.vertDomain.totalCount(), 1u);
    EXPECT_EQ(shaderInfo.fragDomain.totalCount(), 3u);
  }
  EXPECT_TRUE(found);
}

TGFX_TEST(ShaderPermutationTest, QuadConstColorMatchesBothVertexLayouts) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  if (context->backend() == Backend::OpenGL) {
    GTEST_SKIP() << "QuadConstColor is outside the OpenGL stage 1 whitelist";
  }
  auto renderTargetProxy = RenderTargetProxy::Make(context, 8, 8, false);
  ASSERT_NE(renderTargetProxy, nullptr);
  auto renderTarget = renderTargetProxy->getRenderTarget();
  ASSERT_NE(renderTarget, nullptr);

  for (bool hasUVCoord : {false, true}) {
    for (auto inputMode : {InputMode::Ignore, InputMode::ModulateRGBA, InputMode::ModulateA}) {
      BlockAllocator allocator;
      std::optional<Matrix> uvMatrix = Matrix::I();
      if (hasUVCoord) {
        uvMatrix = std::nullopt;
      }
      auto gp = QuadPerEdgeAAGeometryProcessor::Make(&allocator, 8, 8, AAType::Coverage,
                                                     PMColor::White(), uvMatrix, false);
      auto fp = ConstColorProcessor::Make(&allocator, PMColor::Red(), inputMode);
      ASSERT_NE(gp, nullptr);
      ASSERT_NE(fp, nullptr);
      ProgramInfo programInfo(renderTarget.get(), gp.get(), {fp.get()}, 1, nullptr,
                              BlendMode::SrcOver);
      auto match = MatchPermutation(&programInfo);
      ASSERT_TRUE(match.has_value());
      EXPECT_EQ(match->shaderName, "QuadConstColorShader");
      EXPECT_EQ(match->vertPermutationIndex, hasUVCoord ? 1u : 0u);
      EXPECT_EQ(match->fragPermutationIndex, 0u);
    }
  }
}

TGFX_TEST(ShaderPermutationTest, QuadConstColorShaderRegistry) {
  bool found = false;
  for (const auto& factory : ShaderRegistry::All()) {
    auto shader = factory();
    auto info = shader->info();
    if (info.name != "QuadConstColorShader") {
      continue;
    }
    found = true;
    EXPECT_EQ(info.vertDomain.dimensionCount(), 1u);
    EXPECT_EQ(info.vertDomain.totalCount(), 2u);
    EXPECT_EQ(info.fragDomain.totalCount(), 1u);
    EXPECT_EQ(info.vertexFile, "level1/quad_const_color.vert");
    EXPECT_EQ(info.fragmentFile, "level1/quad_const_color.frag");
  }
  EXPECT_TRUE(found);
}

TGFX_TEST(ShaderPermutationTest, ShapeInstancedTextureCoverageMatchesOnlyExactContract) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  if (context->backend() == Backend::OpenGL) {
    GTEST_SKIP() << "ShapeInstancedTextureCoverage is outside the OpenGL stage 1 whitelist";
  }
  auto renderTargetProxy = RenderTargetProxy::Make(context, 8, 8, false);
  ASSERT_NE(renderTargetProxy, nullptr);
  auto textureProxy = context->proxyProvider()->createTextureProxy({}, 8, 8, PixelFormat::ALPHA_8);
  ASSERT_NE(textureProxy, nullptr);
  ASSERT_NE(textureProxy->getTextureView(), nullptr);

  {
    BlockAllocator allocator;
    auto gp = ShapeInstancedGeometryProcessor::Make(&allocator, 8, 8, AAType::None, true,
                                                    Matrix::I(), Matrix::I());
    auto coverage = TextureEffect::Make(&allocator, textureProxy);
    ASSERT_NE(gp, nullptr);
    ASSERT_NE(coverage, nullptr);
    ProgramInfo programInfo(renderTargetProxy->getRenderTarget().get(), gp.get(), {coverage.get()},
                            0, nullptr, BlendMode::SrcOver);
    auto match = MatchPermutation(&programInfo);
    ASSERT_TRUE(match.has_value());
    EXPECT_EQ(match->shaderName, "ShapeInstancedTextureCoverageShader");
    // The bare coverage form is GRADIENT=0, HAS_COLORS=1 in the two-bool domain.
    auto expected = ShapeInstancedTextureCoverageShader::D::domain().encode({0, 1});
    EXPECT_EQ(match->vertPermutationIndex, expected);
    EXPECT_EQ(match->fragPermutationIndex, expected);
  }

  {
    BlockAllocator allocator;
    auto gp = ShapeInstancedGeometryProcessor::Make(&allocator, 8, 8, AAType::None, false,
                                                    Matrix::I(), Matrix::I());
    auto coverage = TextureEffect::Make(&allocator, textureProxy);
    ASSERT_NE(gp, nullptr);
    ASSERT_NE(coverage, nullptr);
    ProgramInfo programInfo(renderTargetProxy->getRenderTarget().get(), gp.get(), {coverage.get()},
                            0, nullptr, BlendMode::SrcOver);
    EXPECT_FALSE(MatchPermutation(&programInfo).has_value());
  }

  {
    BlockAllocator allocator;
    auto gp = ShapeInstancedGeometryProcessor::Make(&allocator, 8, 8, AAType::Coverage, true,
                                                    Matrix::I(), Matrix::I());
    auto coverage = TextureEffect::Make(&allocator, textureProxy);
    ASSERT_NE(gp, nullptr);
    ASSERT_NE(coverage, nullptr);
    ProgramInfo programInfo(renderTargetProxy->getRenderTarget().get(), gp.get(), {coverage.get()},
                            0, nullptr, BlendMode::SrcOver);
    EXPECT_FALSE(MatchPermutation(&programInfo).has_value());
  }

  {
    BlockAllocator allocator;
    auto gp = ShapeInstancedGeometryProcessor::Make(&allocator, 8, 8, AAType::None, true,
                                                    Matrix::I(), Matrix::I());
    auto color = ConstColorProcessor::Make(&allocator, PMColor::Red(), InputMode::Ignore);
    auto coverage = TextureEffect::Make(&allocator, textureProxy);
    ASSERT_NE(gp, nullptr);
    ASSERT_NE(color, nullptr);
    ASSERT_NE(coverage, nullptr);
    ProgramInfo programInfo(renderTargetProxy->getRenderTarget().get(), gp.get(),
                            {color.get(), coverage.get()}, 1, nullptr, BlendMode::SrcOver);
    EXPECT_FALSE(MatchPermutation(&programInfo).has_value());
  }

  {
    BlockAllocator allocator;
    auto gp = ShapeInstancedGeometryProcessor::Make(&allocator, 8, 8, AAType::None, true,
                                                    Matrix::I(), Matrix::I());
    auto coverage = TextureEffect::MakeRGBAAA(&allocator, textureProxy, {}, Point{1, 0});
    ASSERT_NE(gp, nullptr);
    ASSERT_NE(coverage, nullptr);
    ProgramInfo programInfo(renderTargetProxy->getRenderTarget().get(), gp.get(), {coverage.get()},
                            0, nullptr, BlendMode::SrcOver);
    EXPECT_FALSE(MatchPermutation(&programInfo).has_value());
  }

  {
    BlockAllocator allocator;
    auto gp = ShapeInstancedGeometryProcessor::Make(&allocator, 8, 8, AAType::None, true,
                                                    Matrix::I(), Matrix::I());
    auto coverage = TextureEffect::Make(&allocator, textureProxy);
    auto xp = PorterDuffXferProcessor::Make(&allocator, BlendMode::SrcOver, {});
    ASSERT_NE(gp, nullptr);
    ASSERT_NE(coverage, nullptr);
    ASSERT_NE(xp, nullptr);
    ProgramInfo programInfo(renderTargetProxy->getRenderTarget().get(), gp.get(), {coverage.get()},
                            0, xp.get(), BlendMode::SrcOver);
    EXPECT_FALSE(MatchPermutation(&programInfo).has_value());
  }
}

TGFX_TEST(ShaderPermutationTest, ShapeInstancedTextureCoverageShaderRegistry) {
  bool found = false;
  for (const auto& factory : ShaderRegistry::All()) {
    auto shader = factory();
    auto info = shader->info();
    if (info.name != "ShapeInstancedTextureCoverageShader") {
      continue;
    }
    found = true;
    EXPECT_EQ(info.vertDomain.totalCount(), 4u);
    EXPECT_EQ(info.fragDomain.totalCount(), 4u);
  }
  EXPECT_TRUE(found);
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

static void ExpectDirectAARectMatch(ProgramInfo* programInfo, const char* shaderName) {
  auto match = MatchPermutation(programInfo);
  ASSERT_TRUE(match.has_value());
  EXPECT_EQ(match->shaderName, shaderName);
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
      auto gp = QuadPerEdgeAAGeometryProcessor::Make(&allocator, 8, 8, AAType::Coverage,
                                                     PMColor::White(), Matrix::I(), false);
      auto clip = AARectEffect::Make(&allocator, Rect::MakeWH(8, 8));
      auto xp = MakeSingleIntervalXP(context, &allocator, xpType);
      ASSERT_NE(gp, nullptr);
      ASSERT_NE(clip, nullptr);
      if (xpType != 0) {
        ASSERT_NE(xp, nullptr);
      }
      ProgramInfo programInfo(renderTarget.get(), gp.get(), {clip.get()}, 0, xp.get(),
                              BlendMode::SrcOver);
      ExpectDirectAARectMatch(&programInfo, "QuadColorFillShader");
    }
    {
      BlockAllocator allocator;
      auto textureProxy =
          context->proxyProvider()->createTextureProxy({}, 8, 8, PixelFormat::RGBA_8888);
      ASSERT_NE(textureProxy, nullptr);
      auto gp = QuadPerEdgeAAGeometryProcessor::Make(&allocator, 8, 8, AAType::Coverage,
                                                     PMColor::White(), Matrix::I(), false);
      auto color = TextureEffect::Make(&allocator, std::move(textureProxy));
      auto clip = AARectEffect::Make(&allocator, Rect::MakeWH(8, 8));
      auto xp = MakeSingleIntervalXP(context, &allocator, xpType);
      ASSERT_NE(gp, nullptr);
      ASSERT_NE(color, nullptr);
      ASSERT_NE(clip, nullptr);
      if (xpType != 0) {
        ASSERT_NE(xp, nullptr);
      }
      ProgramInfo programInfo(renderTarget.get(), gp.get(), {color.get(), clip.get()}, 1, xp.get(),
                              BlendMode::SrcOver);
      ExpectDirectAARectMatch(&programInfo, "QuadTextureFillShader");
    }
    {
      BlockAllocator allocator;
      auto gp = DefaultGeometryProcessor::Make(&allocator, PMColor::White(), 8, 8, AAType::Coverage,
                                               Matrix::I(), Matrix::I());
      auto clip = AARectEffect::Make(&allocator, Rect::MakeWH(8, 8));
      auto xp = MakeSingleIntervalXP(context, &allocator, xpType);
      ASSERT_NE(gp, nullptr);
      ASSERT_NE(clip, nullptr);
      if (xpType != 0) {
        ASSERT_NE(xp, nullptr);
      }
      ProgramInfo programInfo(renderTarget.get(), gp.get(), {clip.get()}, 0, xp.get(),
                              BlendMode::SrcOver);
      ExpectDirectAARectMatch(&programInfo, "SolidColorFillShader");
    }
    {
      BlockAllocator allocator;
      auto gp = HairlineLineGeometryProcessor::Make(&allocator, PMColor::White(), Matrix::I(),
                                                    Matrix::I(), 1.0f, AAType::Coverage);
      auto clip = AARectEffect::Make(&allocator, Rect::MakeWH(8, 8));
      auto xp = MakeSingleIntervalXP(context, &allocator, xpType);
      ASSERT_NE(gp, nullptr);
      ASSERT_NE(clip, nullptr);
      if (xpType != 0) {
        ASSERT_NE(xp, nullptr);
      }
      ProgramInfo programInfo(renderTarget.get(), gp.get(), {clip.get()}, 0, xp.get(),
                              BlendMode::SrcOver);
      ExpectDirectAARectMatch(&programInfo, "HairlineLineShader");
    }
    {
      BlockAllocator allocator;
      auto gp = HairlineQuadGeometryProcessor::Make(&allocator, PMColor::White(), Matrix::I(),
                                                    Matrix::I(), 1.0f, AAType::Coverage);
      auto clip = AARectEffect::Make(&allocator, Rect::MakeWH(8, 8));
      auto xp = MakeSingleIntervalXP(context, &allocator, xpType);
      ASSERT_NE(gp, nullptr);
      ASSERT_NE(clip, nullptr);
      if (xpType != 0) {
        ASSERT_NE(xp, nullptr);
      }
      ProgramInfo programInfo(renderTarget.get(), gp.get(), {clip.get()}, 0, xp.get(),
                              BlendMode::SrcOver);
      ExpectDirectAARectMatch(&programInfo, "HairlineQuadShader");
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
      {"QuadColorFillShader", 1, 6},  {"QuadTextureFillShader", 8, 12},
      {"SolidColorFillShader", 2, 6}, {"HairlineLineShader", 1, 3},
      {"HairlineQuadShader", 1, 3},
  };
  for (const auto& expectedDomain : expected) {
    bool found = false;
    for (const auto& factory : ShaderRegistry::All()) {
      auto shader = factory();
      auto info = shader->info();
      if (info.name != expectedDomain.name) {
        continue;
      }
      found = true;
      EXPECT_EQ(info.vertDomain.totalCount(), expectedDomain.vertexCount);
      EXPECT_EQ(info.fragDomain.totalCount(), expectedDomain.fragmentCount);
    }
    EXPECT_TRUE(found) << expectedDomain.name;
  }
}

TGFX_TEST(ShaderPermutationTest, DirectAARectRejectsComposedCoverage) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto renderTargetProxy = RenderTargetProxy::Make(context, 8, 8, false);
  ASSERT_NE(renderTargetProxy, nullptr);
  BlockAllocator allocator;
  auto gp = DefaultGeometryProcessor::Make(&allocator, PMColor::White(), 8, 8, AAType::None,
                                           Matrix::I(), Matrix::I());
  auto maskProxy = context->proxyProvider()->createTextureProxy({}, 8, 8, PixelFormat::ALPHA_8);
  ASSERT_NE(maskProxy, nullptr);
  auto mask = DeviceSpaceTextureEffect::Make(&allocator, std::move(maskProxy), Matrix::I());
  auto clip = AARectEffect::Make(&allocator, Rect::MakeWH(8, 8));
  auto composed = FragmentProcessor::Compose(&allocator, std::move(mask), std::move(clip));
  ASSERT_NE(gp, nullptr);
  ASSERT_NE(composed, nullptr);
  ProgramInfo programInfo(renderTargetProxy->getRenderTarget().get(), gp.get(), {composed.get()}, 0,
                          nullptr, BlendMode::SrcOver);
  EXPECT_FALSE(MatchPermutation(&programInfo).has_value());
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
  for (const auto& factory : ShaderRegistry::All()) {
    auto shader = factory();
    auto info = shader->info();
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

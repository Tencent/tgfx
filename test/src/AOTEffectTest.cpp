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

#include <array>
#include "base/TGFXTest.h"
#include "core/utils/BlockAllocator.h"
#include "gpu/AOTChainBuilder.h"
#include "gpu/AOTEffect.h"
#include "gpu/AOTEffectDecomposer.h"
#include "gpu/AOTMaterializationPolicy.h"
#include "gpu/AOTPlanExecutor.h"
#include "gpu/ProgramInfo.h"
#include "gpu/ProxyProvider.h"
#include "gpu/processors/AOTPointwiseChainProcessor.h"
#include "gpu/processors/AlphaThresholdFragmentProcessor.h"
#include "gpu/processors/ColorMatrixFragmentProcessor.h"
#include "gpu/processors/ColorSpaceXFormEffect.h"
#include "gpu/processors/ConstColorProcessor.h"
#include "gpu/processors/DeviceSpaceTextureEffect.h"
#include "gpu/processors/LumaFragmentProcessor.h"
#include "gpu/processors/PerlinNoiseFragmentProcessor.h"
#include "gpu/processors/RectEffect.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/processors/TiledTextureEffect.h"
#include "gpu/processors/XfermodeFragmentProcessor.h"
#include "gtest/gtest.h"
#include "tgfx/core/BlendMode.h"
#include "tgfx/core/BytesKey.h"
#include "tgfx/core/ColorSpace.h"
#include "tgfx/core/Shader.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/gpu/PixelFormat.h"
#include "utils/TestUtils.h"

namespace tgfx {

static constexpr std::array<float, 20> IdentityColorMatrix = {
    1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0,
};

class ShapeRejectingFragmentProcessor : public FragmentProcessor {
 public:
  ShapeRejectingFragmentProcessor() : FragmentProcessor(0) {
  }

  std::string name() const override {
    return "ShapeRejectingFragmentProcessor";
  }

  bool lowerToAOT(AOTNodeBuilder*, AOTNodeID, AOTNodeID*) const override {
    return false;
  }

  void emitCode(EmitArgs&) const override {
  }
};

static AOTAxisAnalysis AnalyzeColorProcessor(FragmentProcessor* processor) {
  ProgramInfo programInfo(nullptr, nullptr, {processor}, 1, nullptr, BlendMode::SrcOver);
  return AOTEffectDecomposer::Analyze(&programInfo).color;
}

static PlacementPtr<FragmentProcessor> MakeTextureProcessor(Context* context,
                                                            BlockAllocator* allocator,
                                                            PixelFormat format) {
  auto proxy = context->proxyProvider()->createTextureProxy({}, 2, 2, format);
  if (proxy == nullptr || proxy->getTextureView() == nullptr) {
    return nullptr;
  }
  return TextureEffect::Make(allocator, std::move(proxy));
}

static PlacementPtr<FragmentProcessor> MakeTiledTextureProcessor(
    Context* context, BlockAllocator* allocator, TileMode tileModeX, TileMode tileModeY,
    PixelFormat format, SrcRectConstraint constraint, const std::optional<Rect>& sampleArea,
    const Matrix* uvMatrix = nullptr, ImageOrigin origin = ImageOrigin::TopLeft,
    const SamplingOptions& sampling = SamplingOptions(), bool mipmapped = false) {
  auto proxy = context->proxyProvider()->createTextureProxy({}, 8, 6, format, mipmapped, origin);
  if (proxy == nullptr || proxy->getTextureView() == nullptr) {
    return nullptr;
  }
  SamplingArgs args(tileModeX, tileModeY, sampling, constraint);
  args.sampleArea = sampleArea;
  auto processor = TiledTextureEffect::Make(allocator, std::move(proxy), args, uvMatrix);
  if (processor == nullptr || processor->name() != "TiledTextureEffect") {
    return nullptr;
  }
  return processor;
}

static void ExpectSamplerStateEquals(const SamplerState& actual, const SamplerState& expected) {
  EXPECT_EQ(actual.tileModeX, expected.tileModeX);
  EXPECT_EQ(actual.tileModeY, expected.tileModeY);
  EXPECT_EQ(actual.minFilterMode, expected.minFilterMode);
  EXPECT_EQ(actual.magFilterMode, expected.magFilterMode);
  EXPECT_EQ(actual.mipmapMode, expected.mipmapMode);
}

static void ExpectResolvedSamplingMatchesLowering(
    const TiledTextureEffect::ResolvedSampling& resolved, const AOTTextureParameters& parameters) {
  ASSERT_EQ(parameters.samplingKind, AOTTextureSamplingKind::Tiled);
  ASSERT_TRUE(parameters.tiledRecipe.has_value());
  const auto& recipe = *parameters.tiledRecipe;
  ExpectSamplerStateEquals(recipe.hardwareSampler, resolved.hardwareSampler);
  ExpectSamplerStateEquals(parameters.samplerState, resolved.hardwareSampler);
  EXPECT_EQ(recipe.shaderModeX, resolved.shaderModeX);
  EXPECT_EQ(recipe.shaderModeY, resolved.shaderModeY);
  EXPECT_EQ(recipe.shaderSubset, resolved.shaderSubset);
  EXPECT_EQ(recipe.shaderClamp, resolved.shaderClamp);
  EXPECT_EQ(recipe.shaderDimensions, resolved.shaderDimensions);
  EXPECT_EQ(recipe.usesShaderDimensions, resolved.usesShaderDimensions);
  EXPECT_EQ(recipe.strict, resolved.strict);
  for (int index = 0; index < 9; ++index) {
    EXPECT_FLOAT_EQ(recipe.coordMatrix[index], resolved.coordMatrix[index]);
    EXPECT_FLOAT_EQ(parameters.uvMatrix[index], resolved.coordMatrix[index]);
  }
  EXPECT_EQ(recipe.hasPerspective, resolved.hasPerspective);
  EXPECT_EQ(recipe.alphaOnly, resolved.alphaOnly);
  EXPECT_EQ(recipe.textureOrigin, resolved.textureOrigin);
  EXPECT_EQ(parameters.hasPerspective, resolved.hasPerspective);
  EXPECT_EQ(parameters.isAlphaOnly, resolved.alphaOnly);
}

static bool BuildTripleGraph(Context* context, BlockAllocator* allocator, AOTEffectGraph* graph) {
  auto texture = MakeTextureProcessor(context, allocator, PixelFormat::RGBA_8888);
  auto colorMatrix = ColorMatrixFragmentProcessor::Make(allocator, IdentityColorMatrix);
  auto luma = LumaFragmentProcessor::Make(allocator);
  if (texture == nullptr || colorMatrix == nullptr || luma == nullptr) {
    return false;
  }
  return AOTEffectDecomposer::Lower({texture.get(), colorMatrix.get(), luma.get()}, graph);
}

TGFX_TEST(AOTEffectTest, BuilderRejectsInvalidInput) {
  AOTNodeBuilder builder;
  AOTNodeID geometry;
  ASSERT_TRUE(builder.addGeometryColor(&geometry));
  AOTColorMatrixParameters parameters = {IdentityColorMatrix};
  AOTNodeID output;
  EXPECT_FALSE(builder.addColorMatrix(AOTNodeID(9), parameters, &output));
  EXPECT_EQ(builder.nodeCount(), 1u);
}

TGFX_TEST(AOTEffectTest, TextureInputUsageMatchesAlphaLayout) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  BlockAllocator allocator;
  auto rgbaTexture = MakeTextureProcessor(context, &allocator, PixelFormat::RGBA_8888);
  auto alphaTexture = MakeTextureProcessor(context, &allocator, PixelFormat::ALPHA_8);
  ASSERT_NE(rgbaTexture, nullptr);
  ASSERT_NE(alphaTexture, nullptr);

  AOTNodeBuilder builder;
  AOTNodeID geometry;
  AOTNodeID rgbaNode;
  AOTNodeID alphaNode;
  ASSERT_TRUE(builder.addGeometryColor(&geometry));
  ASSERT_TRUE(rgbaTexture->lowerToAOT(&builder, geometry, &rgbaNode));
  ASSERT_TRUE(alphaTexture->lowerToAOT(&builder, rgbaNode, &alphaNode));
  AOTEffectGraph graph;
  ASSERT_TRUE(builder.finish(alphaNode, &graph));
  EXPECT_EQ(graph.nodeAt(rgbaNode)->traits.inputUsage, EffectInputUsage::ColorAlpha);
  EXPECT_EQ(graph.nodeAt(alphaNode)->traits.inputUsage, EffectInputUsage::ColorRGBA);
}

TGFX_TEST(AOTEffectTest, ProcessorChainLowersTypedParameters) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  BlockAllocator allocator;
  auto texture = MakeTextureProcessor(context, &allocator, PixelFormat::RGBA_8888);
  auto colorMatrix = ColorMatrixFragmentProcessor::Make(&allocator, IdentityColorMatrix);
  auto colorSpace = ColorSpace::MakeCICP(ColorSpacePrimariesID::Rec601, TransferFunctionID::Rec601);
  auto luma = LumaFragmentProcessor::Make(&allocator, colorSpace);
  ASSERT_NE(texture, nullptr);
  ASSERT_NE(colorMatrix, nullptr);
  ASSERT_NE(luma, nullptr);

  AOTEffectGraph graph;
  ASSERT_TRUE(AOTEffectDecomposer::Lower({texture.get(), colorMatrix.get(), luma.get()}, &graph));
  ASSERT_EQ(graph.nodeCount(), 4u);
  EXPECT_EQ(graph.root(), AOTNodeID(3));
  EXPECT_EQ(graph.nodeAt(AOTNodeID(0))->kind, AOTEffectKind::GeometryColor);
  EXPECT_EQ(graph.nodeAt(AOTNodeID(1))->kind, AOTEffectKind::TextureSource);
  EXPECT_EQ(graph.nodeAt(AOTNodeID(2))->kind, AOTEffectKind::ColorMatrix);
  EXPECT_EQ(graph.nodeAt(AOTNodeID(3))->kind, AOTEffectKind::Luma);

  auto textureParameters =
      std::get_if<AOTTextureParameters>(&graph.nodeAt(AOTNodeID(1))->parameters);
  ASSERT_NE(textureParameters, nullptr);
  EXPECT_EQ(textureParameters->samplingKind, AOTTextureSamplingKind::Plain);
  EXPECT_FALSE(textureParameters->tiledRecipe.has_value());
  auto matrixParameters =
      std::get_if<AOTColorMatrixParameters>(&graph.nodeAt(AOTNodeID(2))->parameters);
  auto lumaParameters = std::get_if<AOTLumaParameters>(&graph.nodeAt(AOTNodeID(3))->parameters);
  ASSERT_NE(matrixParameters, nullptr);
  ASSERT_NE(lumaParameters, nullptr);
  EXPECT_EQ(matrixParameters->matrix, IdentityColorMatrix);
  EXPECT_FLOAT_EQ(lumaParameters->kr, 0.299f);
  EXPECT_FLOAT_EQ(lumaParameters->kg, 0.587f);
  EXPECT_FLOAT_EQ(lumaParameters->kb, 0.114f);
}

TGFX_TEST(AOTEffectTest, ChannelPermutationClassification) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  BlockAllocator allocator;
  std::array<float, 20> swapRedBlue = {0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0};
  auto permutation = ColorMatrixFragmentProcessor::Make(&allocator, swapRedBlue);
  ASSERT_NE(permutation, nullptr);
  EXPECT_TRUE(permutation->isChannelPermutation());

  std::array<float, 20> scaleRed = IdentityColorMatrix;
  scaleRed[0] = 0.5f;
  auto scale = ColorMatrixFragmentProcessor::Make(&allocator, scaleRed);
  ASSERT_NE(scale, nullptr);
  EXPECT_FALSE(scale->isChannelPermutation());
  BytesKey permutationKey = {};
  BytesKey generalKey = {};
  permutation->computeProcessorKey(context, &permutationKey);
  scale->computeProcessorKey(context, &generalKey);
  EXPECT_FALSE(permutationKey == generalKey);

  std::array<float, 20> duplicateRed = IdentityColorMatrix;
  duplicateRed[5] = 1.0f;
  duplicateRed[6] = 0.0f;
  auto duplicate = ColorMatrixFragmentProcessor::Make(&allocator, duplicateRed);
  ASSERT_NE(duplicate, nullptr);
  EXPECT_FALSE(duplicate->isChannelPermutation());

  std::array<float, 20> biased = IdentityColorMatrix;
  biased[4] = 0.1f;
  auto bias = ColorMatrixFragmentProcessor::Make(&allocator, biased);
  ASSERT_NE(bias, nullptr);
  EXPECT_FALSE(bias->isChannelPermutation());
}

TGFX_TEST(AOTEffectTest, UnsupportedProcessorFailsAtomically) {
  BlockAllocator allocator;
  ShapeRejectingFragmentProcessor unsupported;
  AOTEffectGraph graph;
  EXPECT_FALSE(AOTEffectDecomposer::Lower({&unsupported}, &graph));
  EXPECT_EQ(graph.nodeCount(), 0u);
  EXPECT_FALSE(graph.root().isValid());
}

TGFX_TEST(AOTEffectTest, TiledHardwareSamplingSnapshotMatchesLowering) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  BlockAllocator allocator;
  auto processor =
      MakeTiledTextureProcessor(context, &allocator, TileMode::Repeat, TileMode::Clamp,
                                PixelFormat::RGBA_8888, SrcRectConstraint::Fast, std::nullopt);
  ASSERT_NE(processor, nullptr);
  auto tiled = static_cast<TiledTextureEffect*>(processor.get());
  auto resolved = tiled->resolveSampling();
  ASSERT_NE(resolved, nullptr);
  EXPECT_EQ(tiled->resolveSampling(), resolved);
  BytesKey keyBeforeLowering = {};
  processor->computeProcessorKey(context, &keyBeforeLowering);
  EXPECT_EQ(resolved->shaderModeX, TiledTextureEffect::ShaderMode::None);
  EXPECT_EQ(resolved->shaderModeY, TiledTextureEffect::ShaderMode::None);
  EXPECT_EQ(resolved->hardwareSampler.tileModeX, TileMode::Repeat);
  EXPECT_EQ(resolved->hardwareSampler.tileModeY, TileMode::Clamp);
  EXPECT_FALSE(resolved->usesShaderDimensions);
  ExpectSamplerStateEquals(processor->samplerStateAt(0), resolved->hardwareSampler);

  AOTEffectGraph graph;
  ASSERT_TRUE(AOTEffectDecomposer::Lower({processor.get()}, &graph));
  auto parameters = std::get_if<AOTTextureParameters>(&graph.nodeAt(AOTNodeID(1))->parameters);
  ASSERT_NE(parameters, nullptr);
  ExpectResolvedSamplingMatchesLowering(*resolved, *parameters);
  EXPECT_EQ(tiled->resolveSampling(), resolved);
  BytesKey keyAfterLowering = {};
  processor->computeProcessorKey(context, &keyAfterLowering);
  EXPECT_TRUE(keyAfterLowering == keyBeforeLowering);
  AOTEffectPlan plan;
  ASSERT_TRUE(AOTEffectDecomposer::Decompose(graph, &plan));
  ASSERT_EQ(plan.passes.size(), 1u);
  EXPECT_EQ(plan.passes[0].kernel, AOTKernelKind::PointwiseChain);
  // Hardware-resolved tiling (shader mode None on both axes) is chain-compatible: the kernel
  // samples through the resolved hardware sampler with a no-op subset clamp.
  EXPECT_TRUE(AOTPlanExecutor::CanExecute(graph, plan));
}

TGFX_TEST(AOTEffectTest, TiledShaderModesAndStrictSubsetMatchLowering) {
  struct ModeCase {
    TileMode tileModeX;
    TileMode tileModeY;
    SamplingOptions sampling;
    bool mipmapped;
    TiledTextureEffect::ShaderMode shaderModeX;
  };
  const std::array<ModeCase, 8> cases = {
      ModeCase{TileMode::Clamp, TileMode::Repeat,
               SamplingOptions(FilterMode::Linear, MipmapMode::None), false,
               TiledTextureEffect::ShaderMode::Clamp},
      ModeCase{TileMode::Repeat, TileMode::Clamp,
               SamplingOptions(FilterMode::Nearest, MipmapMode::None), false,
               TiledTextureEffect::ShaderMode::RepeatNearestNone},
      ModeCase{TileMode::Repeat, TileMode::Clamp,
               SamplingOptions(FilterMode::Linear, MipmapMode::None), false,
               TiledTextureEffect::ShaderMode::RepeatLinearNone},
      ModeCase{TileMode::Repeat, TileMode::Clamp,
               SamplingOptions(FilterMode::Linear, MipmapMode::Linear), true,
               TiledTextureEffect::ShaderMode::RepeatLinearMipmap},
      ModeCase{TileMode::Repeat, TileMode::Clamp,
               SamplingOptions(FilterMode::Nearest, MipmapMode::Nearest), true,
               TiledTextureEffect::ShaderMode::RepeatNearestMipmap},
      ModeCase{TileMode::Mirror, TileMode::Clamp,
               SamplingOptions(FilterMode::Linear, MipmapMode::None), false,
               TiledTextureEffect::ShaderMode::MirrorRepeat},
      ModeCase{TileMode::Decal, TileMode::Clamp,
               SamplingOptions(FilterMode::Nearest, MipmapMode::None), false,
               TiledTextureEffect::ShaderMode::ClampToBorderNearest},
      ModeCase{TileMode::Decal, TileMode::Clamp,
               SamplingOptions(FilterMode::Linear, MipmapMode::None), false,
               TiledTextureEffect::ShaderMode::ClampToBorderLinear},
  };
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  BlockAllocator allocator;
  auto sampleArea = Rect::MakeXYWH(1, 1, 6, 4);
  for (const auto& modeCase : cases) {
    auto processor = MakeTiledTextureProcessor(
        context, &allocator, modeCase.tileModeX, modeCase.tileModeY, PixelFormat::RGBA_8888,
        SrcRectConstraint::Strict, sampleArea, nullptr, ImageOrigin::TopLeft, modeCase.sampling,
        modeCase.mipmapped);
    ASSERT_NE(processor, nullptr);
    auto tiled = static_cast<TiledTextureEffect*>(processor.get());
    auto resolved = tiled->resolveSampling();
    ASSERT_NE(resolved, nullptr);
    EXPECT_EQ(resolved->shaderModeX, modeCase.shaderModeX);
    EXPECT_EQ(resolved->hardwareSampler.tileModeX, TileMode::Clamp);
    EXPECT_EQ(resolved->hardwareSampler.minFilterMode, modeCase.sampling.minFilterMode);
    EXPECT_EQ(resolved->hardwareSampler.magFilterMode, modeCase.sampling.magFilterMode);
    EXPECT_EQ(resolved->hardwareSampler.mipmapMode, modeCase.sampling.mipmapMode);
    EXPECT_TRUE(resolved->strict);

    AOTEffectGraph graph;
    ASSERT_TRUE(AOTEffectDecomposer::Lower({processor.get()}, &graph));
    auto parameters = std::get_if<AOTTextureParameters>(&graph.nodeAt(AOTNodeID(1))->parameters);
    ASSERT_NE(parameters, nullptr);
    EXPECT_TRUE(parameters->hasSubset);
    ExpectResolvedSamplingMatchesLowering(*resolved, *parameters);
    AOTEffectPlan plan;
    // Mipmap-repeat modes (4,5) must be rejected before publishing a DAG candidate.
    bool chainCompatible =
        modeCase.shaderModeX != TiledTextureEffect::ShaderMode::RepeatLinearMipmap &&
        modeCase.shaderModeX != TiledTextureEffect::ShaderMode::RepeatNearestMipmap;
    ASSERT_EQ(AOTEffectDecomposer::Decompose(graph, &plan), chainCompatible);
    if (chainCompatible) {
      ASSERT_EQ(plan.passes.size(), 1u);
      EXPECT_EQ(plan.passes[0].kernel, AOTKernelKind::PointwiseChain);
      EXPECT_TRUE(AOTPlanExecutor::CanExecute(graph, plan));
    } else {
      plan.output = graph.root();
      AOTPassDescriptor unsupported = {};
      unsupported.kernel = AOTKernelKind::PointwiseChain;
      unsupported.output = graph.root();
      unsupported.nodes = {AOTNodeID(1)};
      plan.passes.push_back(unsupported);
      EXPECT_FALSE(AOTPlanExecutor::CanExecute(graph, plan));
    }
  }
}

TGFX_TEST(AOTEffectTest, ChainRejectsTwoShaderTiledLeaves) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  BlockAllocator allocator;
  auto sampleArea = Rect::MakeXYWH(1, 1, 6, 4);
  // Decal over a strict subset resolves to ClampToBorderLinear on Metal: both leaves are
  // individually chain-compatible, but PointwiseChainShader has only one shared tiled-uniform
  // block. CanExecute must therefore agree with BuildChainFP and reject the pair.
  auto src =
      MakeTiledTextureProcessor(context, &allocator, TileMode::Decal, TileMode::Clamp,
                                PixelFormat::RGBA_8888, SrcRectConstraint::Strict, sampleArea);
  auto dst =
      MakeTiledTextureProcessor(context, &allocator, TileMode::Decal, TileMode::Clamp,
                                PixelFormat::RGBA_8888, SrcRectConstraint::Strict, sampleArea);
  ASSERT_NE(src, nullptr);
  ASSERT_NE(dst, nullptr);
  auto blend = XfermodeFragmentProcessor::MakeFromTwoProcessors(&allocator, std::move(src),
                                                                std::move(dst), BlendMode::SrcOver);
  ASSERT_NE(blend, nullptr);

  AOTEffectGraph graph;
  ASSERT_TRUE(AOTEffectDecomposer::Lower({blend.get()}, &graph));
  AOTEffectPlan plan;
  EXPECT_FALSE(AOTEffectDecomposer::Decompose(graph, &plan));
  plan.output = graph.root();
  AOTPassDescriptor unsupported = {};
  unsupported.kernel = AOTKernelKind::PointwiseChain;
  unsupported.output = graph.root();
  for (uint32_t index = 1; index < graph.nodeCount(); ++index) {
    if (graph.nodeAt(AOTNodeID(index))->kind != AOTEffectKind::GeometryColorOpaqueInput) {
      unsupported.nodes.push_back(AOTNodeID(index));
    }
  }
  plan.passes.push_back(unsupported);
  EXPECT_FALSE(AOTPlanExecutor::CanExecute(graph, plan));
}

TGFX_TEST(AOTEffectTest, TiledAlphaOnlySnapshotMatchesLowering) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  BlockAllocator allocator;
  auto processor =
      MakeTiledTextureProcessor(context, &allocator, TileMode::Repeat, TileMode::Clamp,
                                PixelFormat::ALPHA_8, SrcRectConstraint::Fast, std::nullopt);
  ASSERT_NE(processor, nullptr);
  auto resolved = static_cast<TiledTextureEffect*>(processor.get())->resolveSampling();
  ASSERT_NE(resolved, nullptr);
  EXPECT_TRUE(resolved->alphaOnly);

  AOTEffectGraph graph;
  ASSERT_TRUE(AOTEffectDecomposer::Lower({processor.get()}, &graph));
  auto parameters = std::get_if<AOTTextureParameters>(&graph.nodeAt(AOTNodeID(1))->parameters);
  ASSERT_NE(parameters, nullptr);
  ExpectResolvedSamplingMatchesLowering(*resolved, *parameters);
}

TGFX_TEST(AOTEffectTest, TiledPerspectiveSnapshotMatchesLowering) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  BlockAllocator allocator;
  auto perspective = Matrix::MakeAll(1, 0, 0, 0, 1, 0, 0.01f, 0, 1);
  auto processor = MakeTiledTextureProcessor(context, &allocator, TileMode::Repeat, TileMode::Clamp,
                                             PixelFormat::RGBA_8888, SrcRectConstraint::Fast,
                                             std::nullopt, &perspective);
  ASSERT_NE(processor, nullptr);
  auto resolved = static_cast<TiledTextureEffect*>(processor.get())->resolveSampling();
  ASSERT_NE(resolved, nullptr);
  EXPECT_TRUE(resolved->hasPerspective);

  AOTEffectGraph graph;
  ASSERT_TRUE(AOTEffectDecomposer::Lower({processor.get()}, &graph));
  auto parameters = std::get_if<AOTTextureParameters>(&graph.nodeAt(AOTNodeID(1))->parameters);
  ASSERT_NE(parameters, nullptr);
  ExpectResolvedSamplingMatchesLowering(*resolved, *parameters);
}

TGFX_TEST(AOTEffectTest, TiledBottomLeftSnapshotResolvesUniformCoordinates) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  BlockAllocator allocator;
  auto sampleArea = Rect::MakeXYWH(1, 1, 6, 3);
  auto processor = MakeTiledTextureProcessor(context, &allocator, TileMode::Repeat, TileMode::Clamp,
                                             PixelFormat::RGBA_8888, SrcRectConstraint::Strict,
                                             sampleArea, nullptr, ImageOrigin::BottomLeft);
  ASSERT_NE(processor, nullptr);
  auto resolved = static_cast<TiledTextureEffect*>(processor.get())->resolveSampling();
  ASSERT_NE(resolved, nullptr);
  EXPECT_TRUE(resolved->usesShaderDimensions);
  EXPECT_EQ(resolved->textureOrigin, ImageOrigin::BottomLeft);
  EXPECT_FLOAT_EQ(resolved->shaderDimensions.x, 0.125f);
  EXPECT_FLOAT_EQ(resolved->shaderDimensions.y, 1.0f / 6.0f);
  EXPECT_EQ(resolved->shaderSubset, Rect::MakeLTRB(1, 2, 7, 5));
  EXPECT_EQ(resolved->shaderClamp, Rect::MakeLTRB(1.5f, 2.5f, 6.5f, 4.5f));

  AOTEffectGraph graph;
  ASSERT_TRUE(AOTEffectDecomposer::Lower({processor.get()}, &graph));
  auto parameters = std::get_if<AOTTextureParameters>(&graph.nodeAt(AOTNodeID(1))->parameters);
  ASSERT_NE(parameters, nullptr);
  ExpectResolvedSamplingMatchesLowering(*resolved, *parameters);
}

TGFX_TEST(AOTEffectTest, AnalysisAcceptsTiledTextureLowering) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  BlockAllocator allocator;
  auto proxy = context->proxyProvider()->createTextureProxy({}, 2, 2, PixelFormat::RGBA_8888);
  ASSERT_NE(proxy, nullptr);
  ASSERT_NE(proxy->getTextureView(), nullptr);
  SamplingArgs samplingArgs(TileMode::Repeat, TileMode::Clamp, SamplingOptions(),
                            SrcRectConstraint::Fast);
  auto tiled = TiledTextureEffect::Make(&allocator, std::move(proxy), samplingArgs);
  ASSERT_NE(tiled, nullptr);
  auto xfermode = XfermodeFragmentProcessor::MakeFromDstProcessor(&allocator, std::move(tiled),
                                                                  BlendMode::Multiply);
  auto constColor = ConstColorProcessor::Make(&allocator, PMColor::White(), InputMode::Ignore);
  ASSERT_NE(xfermode, nullptr);
  ASSERT_NE(constColor, nullptr);
  auto composed =
      FragmentProcessor::Compose(&allocator, std::move(constColor), std::move(xfermode));
  ASSERT_NE(composed, nullptr);

  auto analysis = AnalyzeColorProcessor(composed.get());
  EXPECT_EQ(analysis.outcome, AOTDecomposeOutcome::FusablePointwise);
  EXPECT_TRUE(analysis.blockingProcessor.empty());
}

TGFX_TEST(AOTEffectTest, LoweringShapeRejectionIsUnsupportedShape) {
  ShapeRejectingFragmentProcessor processor;
  auto analysis = AnalyzeColorProcessor(&processor);
  EXPECT_EQ(analysis.outcome, AOTDecomposeOutcome::UnsupportedShape);
  EXPECT_TRUE(analysis.blockingProcessor.empty());
}

TGFX_TEST(AOTEffectTest, ColorMatrixChainFusesToSinglePass) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  BlockAllocator allocator;
  auto texture = MakeTextureProcessor(context, &allocator, PixelFormat::RGBA_8888);
  auto colorMatrix = ColorMatrixFragmentProcessor::Make(&allocator, IdentityColorMatrix);
  ASSERT_NE(texture, nullptr);
  ASSERT_NE(colorMatrix, nullptr);
  AOTEffectGraph graph;
  ASSERT_TRUE(AOTEffectDecomposer::Lower({texture.get(), colorMatrix.get()}, &graph));

  AOTEffectPlan plan;
  ASSERT_TRUE(AOTEffectDecomposer::Decompose(graph, &plan));
  ASSERT_EQ(plan.passes.size(), 1u);
  EXPECT_EQ(plan.passes[0].kernel, AOTKernelKind::PointwiseChain);
  EXPECT_EQ(plan.passes[0].nodes, std::vector<AOTNodeID>({AOTNodeID(1), AOTNodeID(2)}));
  EXPECT_FALSE(plan.passes[0].materializesOutput);
}

TGFX_TEST(AOTEffectTest, TripleChainFusesToSinglePass) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  BlockAllocator allocator;
  AOTEffectGraph graph;
  ASSERT_TRUE(BuildTripleGraph(context, &allocator, &graph));

  AOTEffectPlan fusedPlan;
  ASSERT_TRUE(AOTEffectDecomposer::Decompose(graph, &fusedPlan));
  ASSERT_EQ(fusedPlan.passes.size(), 1u);
  EXPECT_EQ(fusedPlan.passes[0].kernel, AOTKernelKind::PointwiseChain);
  EXPECT_EQ(fusedPlan.passes[0].nodes,
            std::vector<AOTNodeID>({AOTNodeID(1), AOTNodeID(2), AOTNodeID(3)}));
  EXPECT_FALSE(fusedPlan.passes[0].materializesOutput);
  EXPECT_TRUE(fusedPlan.passes[0].dependencies.empty());
}

// A device-space texture chain cannot use the fused chain kernel (it samples in device
// coordinates), so the tail planner serves it in segmented passes. This keeps multi-pass
// planning covered after plain linear chains moved to the single-pass DAG planner.
TGFX_TEST(AOTEffectTest, DeviceSpaceLinearChainUsesPointwiseTailPlanner) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  BlockAllocator allocator;
  auto proxy = context->proxyProvider()->createTextureProxy({}, 2, 2, PixelFormat::RGBA_8888);
  ASSERT_NE(proxy, nullptr);
  ASSERT_NE(proxy->getTextureView(), nullptr);
  auto texture = DeviceSpaceTextureEffect::Make(&allocator, std::move(proxy), Matrix::I());
  auto colorMatrix = ColorMatrixFragmentProcessor::Make(&allocator, IdentityColorMatrix);
  auto luma = LumaFragmentProcessor::Make(&allocator);
  auto colorMatrix2 = ColorMatrixFragmentProcessor::Make(&allocator, IdentityColorMatrix);
  ASSERT_NE(texture, nullptr);
  ASSERT_NE(colorMatrix, nullptr);
  ASSERT_NE(luma, nullptr);
  ASSERT_NE(colorMatrix2, nullptr);
  AOTEffectGraph graph;
  ASSERT_TRUE(AOTEffectDecomposer::Lower(
      {texture.get(), colorMatrix.get(), luma.get(), colorMatrix2.get()}, &graph));

  AOTEffectPlan plan;
  ASSERT_TRUE(AOTEffectDecomposer::Decompose(graph, &plan));
  ASSERT_GT(plan.passes.size(), 1u);
  for (const auto& pass : plan.passes) {
    EXPECT_EQ(pass.kernel, AOTKernelKind::PointwiseTail);
  }
  for (size_t index = 0; index + 1 < plan.passes.size(); ++index) {
    EXPECT_TRUE(plan.passes[index].materializesOutput);
  }
  EXPECT_FALSE(plan.passes.back().materializesOutput);
  EXPECT_TRUE(AOTPlanExecutor::CanExecute(graph, plan));
}

TGFX_TEST(AOTEffectTest, TwoColorSpaceTransformsRetainTailCandidate) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  for (bool insertLuma : {false, true}) {
    BlockAllocator allocator;
    auto texture = MakeTextureProcessor(context, &allocator, PixelFormat::RGBA_8888);
    auto first =
        ColorSpaceXformEffect::Make(&allocator, ColorSpace::SRGB().get(), AlphaType::Premultiplied,
                                    ColorSpace::SRGBLinear().get(), AlphaType::Premultiplied);
    auto second = ColorSpaceXformEffect::Make(&allocator, ColorSpace::SRGBLinear().get(),
                                              AlphaType::Premultiplied, ColorSpace::SRGB().get(),
                                              AlphaType::Premultiplied);
    auto luma = LumaFragmentProcessor::Make(&allocator);
    ASSERT_NE(texture, nullptr);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    std::vector<const FragmentProcessor*> processors = {texture.get(), first.get()};
    if (insertLuma) {
      processors.push_back(luma.get());
    }
    processors.push_back(second.get());
    AOTEffectGraph graph;
    ASSERT_TRUE(AOTEffectDecomposer::Lower(processors, &graph));
    AOTEffectPlan plan;
    ASSERT_TRUE(AOTEffectDecomposer::Decompose(graph, &plan));
    ASSERT_EQ(plan.passes.size(), insertLuma ? 2u : 1u);
    for (const auto& pass : plan.passes) {
      EXPECT_EQ(pass.kernel, AOTKernelKind::PointwiseTail);
    }
    EXPECT_TRUE(AOTPlanExecutor::CanExecute(graph, plan));
    auto rebuilt = AOTChainBuilder::BuildFPForPass(&allocator, graph, plan.passes[0], nullptr);
    EXPECT_NE(rebuilt, nullptr);
    AOTEffectPlan forcedChain = {};
    forcedChain.output = graph.root();
    AOTPassDescriptor pass = {};
    pass.kernel = AOTKernelKind::PointwiseChain;
    pass.output = graph.root();
    for (uint32_t index = 1; index < graph.nodeCount(); ++index) {
      pass.nodes.push_back(AOTNodeID(index));
    }
    forcedChain.passes.push_back(pass);
    EXPECT_FALSE(AOTPlanExecutor::CanExecute(graph, forcedChain));
    EXPECT_EQ(AOTChainBuilder::BuildChainProcessor(&allocator, graph, pass), nullptr);
  }
}

TGFX_TEST(AOTEffectTest, LUTBindingBudgetIsNotOrdinaryLeafBudget) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  for (int textureCount : {0, 1, 2}) {
    AOTNodeBuilder builder;
    AOTNodeID current;
    ASSERT_TRUE(builder.addGeometryColor(&current));
    for (int index = 0; index < textureCount; ++index) {
      AOTTextureParameters texture = {};
      texture.textureProxy =
          context->proxyProvider()->createTextureProxy({}, 2, 2, PixelFormat::RGBA_8888);
      ASSERT_NE(texture.textureProxy, nullptr);
      ASSERT_TRUE(builder.addTextureSource(current, texture, &current));
    }
    AOTGradientParameters gradient = {};
    gradient.colorizerKind = 3;
    gradient.lutProxy =
        context->proxyProvider()->createTextureProxy({}, 2, 2, PixelFormat::RGBA_8888);
    ASSERT_NE(gradient.lutProxy, nullptr);
    ASSERT_TRUE(builder.addGradientSource(current, gradient, &current));
    AOTEffectGraph graph;
    ASSERT_TRUE(builder.finish(current, &graph));
    AOTEffectPlan plan;
    ASSERT_EQ(AOTEffectDecomposer::Decompose(graph, &plan), textureCount < 2);
    if (textureCount < 2) {
      ASSERT_EQ(plan.passes.size(), 1u);
      EXPECT_TRUE(AOTPlanExecutor::CanExecute(graph, plan));
      BlockAllocator allocator;
      EXPECT_NE(AOTChainBuilder::BuildChainProcessor(&allocator, graph, plan.passes[0]), nullptr);
    }
  }
}

TGFX_TEST(AOTEffectTest, LinearChainSlotBoundaryPreservesTailFallback) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  for (size_t opCount : {size_t{31}, size_t{32}}) {
    BlockAllocator allocator;
    auto texture = MakeTextureProcessor(context, &allocator, PixelFormat::RGBA_8888);
    auto matrix = ColorMatrixFragmentProcessor::Make(&allocator, IdentityColorMatrix);
    ASSERT_NE(texture, nullptr);
    ASSERT_NE(matrix, nullptr);
    std::vector<const FragmentProcessor*> processors = {texture.get()};
    processors.insert(processors.end(), opCount, matrix.get());
    AOTEffectGraph graph;
    ASSERT_TRUE(AOTEffectDecomposer::Lower(processors, &graph));
    AOTEffectPlan plan;
    ASSERT_TRUE(AOTEffectDecomposer::Decompose(graph, &plan));
    // The chain kernel carries 32 instructions with a recycled 16-entry register file, so a
    // texture plus 31 operators fuses into one pass; the 32nd operator crosses the instruction
    // budget and the tail planner takes over (two operators per pass after the source).
    EXPECT_EQ(plan.passes.size(), opCount == 31 ? 1u : 16u);
    EXPECT_EQ(plan.passes[0].kernel,
              opCount == 31 ? AOTKernelKind::PointwiseChain : AOTKernelKind::PointwiseTail);
    EXPECT_TRUE(AOTPlanExecutor::CanExecute(graph, plan));
  }
}

TGFX_TEST(AOTEffectTest, DecompositionIsDeterministic) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  BlockAllocator allocator;
  AOTEffectGraph graph;
  ASSERT_TRUE(BuildTripleGraph(context, &allocator, &graph));

  AOTEffectPlan first;
  AOTEffectPlan second;
  ASSERT_TRUE(AOTEffectDecomposer::Decompose(graph, &first));
  ASSERT_TRUE(AOTEffectDecomposer::Decompose(graph, &second));
  ASSERT_EQ(first.passes.size(), second.passes.size());
  for (size_t index = 0; index < first.passes.size(); ++index) {
    EXPECT_EQ(first.passes[index].kernel, second.passes[index].kernel);
    EXPECT_EQ(first.passes[index].nodes, second.passes[index].nodes);
    EXPECT_EQ(first.passes[index].dependencies, second.passes[index].dependencies);
    EXPECT_EQ(first.passes[index].output, second.passes[index].output);
    EXPECT_EQ(first.passes[index].materializesOutput, second.passes[index].materializesOutput);
  }
  EXPECT_EQ(first.output, second.output);
}

TGFX_TEST(AOTEffectTest, ConstColorLowersToConstColorNode) {
  BlockAllocator allocator;
  PMColor color = {0.5f, 0.25f, 0.125f, 0.75f};
  auto constColor = ConstColorProcessor::Make(&allocator, color, InputMode::ModulateRGBA);
  ASSERT_NE(constColor, nullptr);

  AOTNodeBuilder builder;
  AOTNodeID geometry;
  AOTNodeID node;
  ASSERT_TRUE(builder.addGeometryColor(&geometry));
  ASSERT_TRUE(constColor->lowerToAOT(&builder, geometry, &node));
  AOTEffectGraph graph;
  ASSERT_TRUE(builder.finish(node, &graph));

  auto constNode = graph.nodeAt(node);
  ASSERT_NE(constNode, nullptr);
  EXPECT_EQ(constNode->kind, AOTEffectKind::ConstColor);
  ASSERT_EQ(constNode->inputs.size(), 1u);
  EXPECT_EQ(constNode->inputs[0], geometry);
  auto parameters = std::get_if<AOTConstColorParameters>(&constNode->parameters);
  ASSERT_NE(parameters, nullptr);
  EXPECT_EQ(parameters->inputMode, static_cast<int>(InputMode::ModulateRGBA));
  EXPECT_FLOAT_EQ(parameters->color[0], 0.5f);
  EXPECT_FLOAT_EQ(parameters->color[3], 0.75f);
}

TGFX_TEST(AOTEffectTest, XfermodeDstLowersToBinaryBlend) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  BlockAllocator allocator;
  auto dst = MakeTextureProcessor(context, &allocator, PixelFormat::RGBA_8888);
  ASSERT_NE(dst, nullptr);
  // DstChild: the input color is src, the child processor supplies dst.
  auto xfermode = XfermodeFragmentProcessor::MakeFromDstProcessor(&allocator, std::move(dst),
                                                                  BlendMode::Multiply);
  ASSERT_NE(xfermode, nullptr);

  AOTEffectGraph graph;
  ASSERT_TRUE(AOTEffectDecomposer::Lower({xfermode.get()}, &graph));
  // node0=geometry, node1=white input (the runtime feeds a single-child xfer's child white),
  // node2=texture(dst, input edge = the white designator), node3=blend(src=geometry, dst=texture).
  ASSERT_EQ(graph.nodeCount(), 4u);
  auto white = graph.nodeAt(AOTNodeID(1));
  ASSERT_NE(white, nullptr);
  EXPECT_EQ(white->kind, AOTEffectKind::GeometryWhiteInput);
  auto texture = graph.nodeAt(AOTNodeID(2));
  ASSERT_NE(texture, nullptr);
  EXPECT_EQ(texture->kind, AOTEffectKind::TextureSource);
  ASSERT_EQ(texture->inputs.size(), 1u);
  EXPECT_EQ(texture->inputs[0], AOTNodeID(1));  // the child lowers from the white input
  auto blend = graph.nodeAt(AOTNodeID(3));
  ASSERT_NE(blend, nullptr);
  EXPECT_EQ(blend->kind, AOTEffectKind::Blend);
  ASSERT_EQ(blend->inputs.size(), 2u);
  EXPECT_EQ(blend->inputs[0], AOTNodeID(0));  // src = input color
  EXPECT_EQ(blend->inputs[1], AOTNodeID(2));  // dst = child texture
  auto parameters = std::get_if<AOTBlendParameters>(&blend->parameters);
  ASSERT_NE(parameters, nullptr);
  EXPECT_EQ(parameters->childType, 0);
  EXPECT_EQ(parameters->blendMode, static_cast<int>(BlendMode::Multiply));
}

TGFX_TEST(AOTEffectTest, XfermodeTwoLowersBothChildren) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  BlockAllocator allocator;
  auto src = MakeTextureProcessor(context, &allocator, PixelFormat::RGBA_8888);
  auto dst = MakeTextureProcessor(context, &allocator, PixelFormat::RGBA_8888);
  ASSERT_NE(src, nullptr);
  ASSERT_NE(dst, nullptr);
  auto xfermode = XfermodeFragmentProcessor::MakeFromTwoProcessors(
      &allocator, std::move(src), std::move(dst), BlendMode::Screen);
  ASSERT_NE(xfermode, nullptr);

  AOTEffectGraph graph;
  ASSERT_TRUE(AOTEffectDecomposer::Lower({xfermode.get()}, &graph));
  // node0=geometry, node1=opaque-alpha geometry input (the runtime feeds the children
  // vec4(inputColor.rgb, 1.0)), node2=src texture(input=1), node3=dst texture(input=1),
  // node4=blend(src=2, dst=3).
  ASSERT_EQ(graph.nodeCount(), 5u);
  auto opaqueInput = graph.nodeAt(AOTNodeID(1));
  ASSERT_NE(opaqueInput, nullptr);
  EXPECT_EQ(opaqueInput->kind, AOTEffectKind::GeometryColorOpaqueInput);
  for (auto index : {2u, 3u}) {
    auto child = graph.nodeAt(AOTNodeID(index));
    ASSERT_NE(child, nullptr);
    ASSERT_EQ(child->inputs.size(), 1u);
    EXPECT_EQ(child->inputs[0], AOTNodeID(1));
  }
  auto blend = graph.nodeAt(AOTNodeID(4));
  ASSERT_NE(blend, nullptr);
  EXPECT_EQ(blend->kind, AOTEffectKind::Blend);
  ASSERT_EQ(blend->inputs.size(), 2u);
  EXPECT_EQ(blend->inputs[0], AOTNodeID(2));  // src child
  EXPECT_EQ(blend->inputs[1], AOTNodeID(3));  // dst child
  auto parameters = std::get_if<AOTBlendParameters>(&blend->parameters);
  ASSERT_NE(parameters, nullptr);
  EXPECT_EQ(parameters->childType, 2);
}

TGFX_TEST(AOTEffectTest, BuilderRejectsBlendWithInvalidOperand) {
  AOTNodeBuilder builder;
  AOTNodeID geometry;
  ASSERT_TRUE(builder.addGeometryColor(&geometry));
  AOTBlendParameters parameters = {};
  AOTNodeID output;
  // Second operand references a non-existent node; addBlend must reject and add nothing.
  EXPECT_FALSE(builder.addBlend(geometry, AOTNodeID(7), parameters, &output));
  EXPECT_EQ(builder.nodeCount(), 1u);
}

TGFX_TEST(AOTEffectTest, BlendTreeDecomposesToPointwiseChain) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  BlockAllocator allocator;
  auto src = MakeTextureProcessor(context, &allocator, PixelFormat::RGBA_8888);
  auto dst = MakeTextureProcessor(context, &allocator, PixelFormat::RGBA_8888);
  ASSERT_NE(src, nullptr);
  ASSERT_NE(dst, nullptr);
  auto xfermode = XfermodeFragmentProcessor::MakeFromTwoProcessors(
      &allocator, std::move(src), std::move(dst), BlendMode::Screen);
  ASSERT_NE(xfermode, nullptr);

  AOTEffectGraph graph;
  ASSERT_TRUE(AOTEffectDecomposer::Lower({xfermode.get()}, &graph));
  // The linear planner cannot represent the two-input blend DAG; the pointwise-DAG planner folds
  // the whole graph into a single PointwiseChain pass covering nodes 1..3.
  AOTEffectPlan plan;
  ASSERT_TRUE(AOTEffectDecomposer::Decompose(graph, &plan));
  ASSERT_EQ(plan.passes.size(), 1u);
  EXPECT_EQ(plan.passes[0].kernel, AOTKernelKind::PointwiseChain);
  EXPECT_FALSE(plan.passes[0].materializesOutput);
  EXPECT_EQ(plan.passes[0].output, graph.root());
  EXPECT_EQ(plan.passes[0].nodes.size(), 3u);
  EXPECT_EQ(plan.output, graph.root());
}

TGFX_TEST(AOTEffectTest, PointwiseDAGUsesProductionSamplerBudget) {
  EXPECT_EQ(MaxFusedAOTSamplers, 4);
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);

  // Build the DAG directly: a geometry color plus N texture leaves folded by single-child
  // blends, so the fused-pass sampler budget is the only thing under test. The leaves after the
  // first are DstChild blend children: the real lowering feeds them the white input designator
  // (XfermodeFragmentProcessor::lowerToAOT), so the hand-built graph does the same.
  auto makeBlendGraph = [&](int textureCount, AOTEffectGraph* graph) {
    AOTNodeBuilder builder = {};
    AOTNodeID geometry = AOTNodeID::Invalid();
    if (!builder.addGeometryColor(&geometry)) {
      return false;
    }
    AOTNodeID white = AOTNodeID::Invalid();
    if (!builder.addGeometryWhiteInput(&white)) {
      return false;
    }
    AOTNodeID current = AOTNodeID::Invalid();
    for (int index = 0; index < textureCount; ++index) {
      auto proxy = context->proxyProvider()->createTextureProxy({}, 2, 2, PixelFormat::RGBA_8888);
      if (proxy == nullptr || proxy->getTextureView() == nullptr) {
        return false;
      }
      AOTTextureParameters textureParams = {};
      textureParams.textureProxy = std::move(proxy);
      AOTNodeID texture = AOTNodeID::Invalid();
      if (!builder.addTextureSource(index == 0 ? geometry : white, textureParams, &texture)) {
        return false;
      }
      if (index == 0) {
        current = texture;
        continue;
      }
      AOTBlendParameters blendParams = {};
      blendParams.blendMode = static_cast<int>(BlendMode::SrcOver);
      blendParams.childType = 0;
      AOTNodeID blended = AOTNodeID::Invalid();
      if (!builder.addBlend(current, texture, blendParams, &blended)) {
        return false;
      }
      current = blended;
    }
    return builder.finish(current, graph);
  };

  for (int count = 1; count <= MaxFusedAOTSamplers; ++count) {
    SCOPED_TRACE(count);
    AOTEffectGraph acceptedGraph;
    ASSERT_TRUE(makeBlendGraph(count, &acceptedGraph));
    AOTEffectPlan acceptedPlan;
    ASSERT_TRUE(AOTEffectDecomposer::Decompose(acceptedGraph, &acceptedPlan));
    ASSERT_EQ(acceptedPlan.passes.size(), 1u);
    EXPECT_TRUE(AOTPlanExecutor::CanExecute(acceptedGraph, acceptedPlan));
    BlockAllocator allocator;
    auto processor =
        AOTChainBuilder::BuildChainProcessor(&allocator, acceptedGraph, acceptedPlan.passes[0]);
    ASSERT_NE(processor, nullptr);
    auto chain = static_cast<const AOTPointwiseChainProcessor*>(processor.get());
    EXPECT_EQ(chain->leafCount(), 4u);
  }

  AOTEffectGraph rejectedGraph;
  ASSERT_TRUE(makeBlendGraph(MaxFusedAOTSamplers + 1, &rejectedGraph));
  AOTEffectPlan rejectedPlan;
  EXPECT_FALSE(AOTEffectDecomposer::Decompose(rejectedGraph, &rejectedPlan));
}

TGFX_TEST(AOTEffectTest, ConstColorChainDecomposesToPointwiseChain) {
  BlockAllocator allocator;
  // A ConstColor(Ignore) source modulated by a color matrix: pure pointwise, no textures.
  auto constColor =
      ConstColorProcessor::Make(&allocator, PMColor{0.4f, 0.3f, 0.2f, 1.0f}, InputMode::Ignore);
  auto colorMatrix = ColorMatrixFragmentProcessor::Make(&allocator, IdentityColorMatrix);
  ASSERT_NE(constColor, nullptr);
  ASSERT_NE(colorMatrix, nullptr);

  AOTEffectGraph graph;
  ASSERT_TRUE(AOTEffectDecomposer::Lower({constColor.get(), colorMatrix.get()}, &graph));
  AOTEffectPlan plan;
  ASSERT_TRUE(AOTEffectDecomposer::Decompose(graph, &plan));
  ASSERT_EQ(plan.passes.size(), 1u);
  EXPECT_EQ(plan.passes[0].kernel, AOTKernelKind::PointwiseChain);
  // A zero-leaf chain is executable: the kernel's TEXTURE_COUNT domain encodes it, evaluating
  // const-color and blend ops against the geometry color alone.
  EXPECT_TRUE(AOTPlanExecutor::CanExecute(graph, plan));
  EXPECT_TRUE(AOTChainBuilder::BuildChainProcessor(&allocator, graph, plan.passes[0]) != nullptr);
}

// A plain linear texture chain fits the fused chain kernel's contracts, so the DAG planner
// serves it in one pass (the tail planner would split it into segments bounded by its fixed
// two-slot shader, materializing intermediates unnecessarily).
TGFX_TEST(AOTEffectTest, LinearTextureChainPrefersSinglePassChain) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  BlockAllocator allocator;
  AOTEffectGraph graph;
  ASSERT_TRUE(BuildTripleGraph(context, &allocator, &graph));
  AOTEffectPlan plan;
  ASSERT_TRUE(AOTEffectDecomposer::Decompose(graph, &plan));
  ASSERT_EQ(plan.passes.size(), 1u);
  EXPECT_EQ(plan.passes[0].kernel, AOTKernelKind::PointwiseChain);
  EXPECT_EQ(plan.passes[0].nodes,
            std::vector<AOTNodeID>({AOTNodeID(1), AOTNodeID(2), AOTNodeID(3)}));
  EXPECT_TRUE(AOTPlanExecutor::CanExecute(graph, plan));
}

// M1.0 geodesic probe: the multi-pass executor rebuilds a TextureEffect from the AOTTextureParameters
// captured by lowerToAOT (rather than reusing the original FP). This verifies the round-trip is
// lossless — the rebuilt effect must be byte-identical to the original, otherwise the AOT multi-pass
// path would diverge from the JIT single-pass render. The processor key encodes sampler state,
// subset, uv matrix, alphaStart and format, so equal keys prove equivalent shader + uniform layout.
static PlacementPtr<FragmentProcessor> RebuildTextureFromAOTParameters(
    BlockAllocator* allocator, const AOTTextureParameters& parameters) {
  SamplingOptions sampling(parameters.samplerState.minFilterMode,
                           parameters.samplerState.magFilterMode,
                           parameters.samplerState.mipmapMode);
  SamplingArgs args = {parameters.samplerState.tileModeX, parameters.samplerState.tileModeY,
                       sampling, parameters.constraint};
  args.sampleArea = parameters.subset;
  auto uvMatrix = parameters.uvMatrix;
  if (parameters.hasRGBAAA) {
    return TextureEffect::MakeRGBAAA(allocator, parameters.textureProxy, args,
                                     parameters.alphaStart, &uvMatrix);
  }
  return TextureEffect::Make(allocator, parameters.textureProxy, args, &uvMatrix);
}

TGFX_TEST(AOTEffectTest, TextureParametersRebuildIsLossless) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  BlockAllocator allocator;
  auto original = MakeTextureProcessor(context, &allocator, PixelFormat::RGBA_8888);
  ASSERT_NE(original, nullptr);

  AOTEffectGraph graph;
  ASSERT_TRUE(AOTEffectDecomposer::Lower({original.get()}, &graph));
  ASSERT_EQ(graph.nodeCount(), 2u);
  auto parameters = std::get_if<AOTTextureParameters>(&graph.nodeAt(AOTNodeID(1))->parameters);
  ASSERT_NE(parameters, nullptr);

  auto rebuilt = RebuildTextureFromAOTParameters(&allocator, *parameters);
  ASSERT_NE(rebuilt, nullptr);

  BytesKey originalKey = {};
  BytesKey rebuiltKey = {};
  original->computeProcessorKey(context, &originalKey);
  rebuilt->computeProcessorKey(context, &rebuiltKey);
  EXPECT_TRUE(originalKey == rebuiltKey);
}

static MaterializationDecision EvaluateBlendChild(const FragmentProcessor* child,
                                                  size_t childIndex) {
  return AOTMaterializationPolicy::Evaluate(child, MaterializationConsumer::PointwiseBlend,
                                            childIndex);
}

// Locks the two predicates the materialization policy reports for a pointwise blend child. They
// differ on exactly one input: a TiledTextureEffect at the src position is a legal inline child, so
// correctness does not force materializing it, yet no precompiled blend kernel covers that
// permutation, so AOT still wants it materialized. Nothing else may separate the two.
TGFX_TEST(AOTEffectTest, PointwiseBlendChildPolicy) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  BlockAllocator allocator;

  // An absent child needs nothing.
  for (size_t childIndex : {size_t(0), size_t(1)}) {
    auto decision = EvaluateBlendChild(nullptr, childIndex);
    EXPECT_FALSE(decision.requiredForCorrectness);
    EXPECT_FALSE(decision.shouldFlatten);
  }

  // A plain sampled texture is both legal and matchable at either position.
  auto texture = MakeTextureProcessor(context, &allocator, PixelFormat::RGBA_8888);
  ASSERT_NE(texture, nullptr);
  for (size_t childIndex : {size_t(0), size_t(1)}) {
    auto decision = EvaluateBlendChild(texture.get(), childIndex);
    EXPECT_FALSE(decision.requiredForCorrectness);
    EXPECT_FALSE(decision.shouldFlatten);
  }

  // A constant color is a matchable src, but the kernels carry no constant-color dst.
  auto constColor = ConstColorProcessor::Make(&allocator, PMColor::White(), InputMode::Ignore);
  ASSERT_NE(constColor, nullptr);
  auto constSrc = EvaluateBlendChild(constColor.get(), 0);
  EXPECT_FALSE(constSrc.requiredForCorrectness);
  EXPECT_FALSE(constSrc.shouldFlatten);
  auto constDst = EvaluateBlendChild(constColor.get(), 1);
  EXPECT_TRUE(constDst.requiredForCorrectness);
  EXPECT_TRUE(constDst.shouldFlatten);

  // The one input where the two predicates disagree.
  auto tiled =
      MakeTiledTextureProcessor(context, &allocator, TileMode::Repeat, TileMode::Repeat,
                                PixelFormat::RGBA_8888, SrcRectConstraint::Fast, std::nullopt);
  ASSERT_NE(tiled, nullptr);
  auto tiledSrc = EvaluateBlendChild(tiled.get(), 0);
  EXPECT_FALSE(tiledSrc.requiredForCorrectness);
  EXPECT_TRUE(tiledSrc.shouldFlatten);
  auto tiledDst = EvaluateBlendChild(tiled.get(), 1);
  EXPECT_TRUE(tiledDst.requiredForCorrectness);
  EXPECT_TRUE(tiledDst.shouldFlatten);

  // The apron is a property of the consumer, so it holds whatever the child is.
  EXPECT_EQ(EvaluateBlendChild(texture.get(), 0).apronRadius, 1.0f);
  EXPECT_EQ(EvaluateBlendChild(nullptr, 1).apronRadius, 1.0f);
}

static PlacementPtr<FragmentProcessor> MakePerlinProcessor(Context* context) {
  auto shader = Shader::MakeTurbulence(0.05f, 0.05f, 3, 6903);
  if (shader == nullptr) {
    return nullptr;
  }
  FPArgs args(context, 0, Rect::MakeWH(64.0f, 64.0f));
  return FragmentProcessor::Make(shader, args);
}

TGFX_TEST(AOTEffectTest, PerlinNoiseLowersToPerlinNoiseSourceNode) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  BlockAllocator allocator;
  auto perlin = MakePerlinProcessor(context);
  ASSERT_NE(perlin, nullptr);

  AOTEffectGraph graph;
  ASSERT_TRUE(AOTEffectDecomposer::Lower({perlin.get()}, &graph));
  ASSERT_EQ(graph.nodeCount(), 2u);
  auto node = graph.nodeAt(AOTNodeID(1));
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(node->kind, AOTEffectKind::PerlinNoiseSource);
  auto parameters = std::get_if<AOTPerlinNoiseParameters>(&node->parameters);
  ASSERT_NE(parameters, nullptr);
  EXPECT_NE(parameters->permutationsView, nullptr);
  EXPECT_NE(parameters->noiseView, nullptr);
  EXPECT_EQ(parameters->numOctaves, 3);
}

TGFX_TEST(AOTEffectTest, BarePerlinNoiseUsesPerlinNoiseFillPlanner) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  BlockAllocator allocator;
  auto perlin = MakePerlinProcessor(context);
  ASSERT_NE(perlin, nullptr);

  AOTEffectGraph graph;
  ASSERT_TRUE(AOTEffectDecomposer::Lower({perlin.get()}, &graph));
  AOTEffectPlan plan;
  ASSERT_TRUE(AOTEffectDecomposer::Decompose(graph, &plan));
  ASSERT_EQ(plan.passes.size(), 1u);
  EXPECT_EQ(plan.passes[0].kernel, AOTKernelKind::PerlinNoiseFill);
  EXPECT_EQ(plan.passes[0].nodes, std::vector<AOTNodeID>({AOTNodeID(1)}));
  EXPECT_FALSE(plan.passes[0].materializesOutput);
  EXPECT_TRUE(AOTPlanExecutor::CanExecute(graph, plan));
}

TGFX_TEST(AOTEffectTest, PerlinNoisePlusOneOpFusesToSinglePass) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  BlockAllocator allocator;
  auto perlin = MakePerlinProcessor(context);
  ASSERT_NE(perlin, nullptr);
  auto colorMatrix = ColorMatrixFragmentProcessor::Make(&allocator, IdentityColorMatrix);
  ASSERT_NE(colorMatrix, nullptr);

  AOTEffectGraph graph;
  ASSERT_TRUE(AOTEffectDecomposer::Lower({perlin.get(), colorMatrix.get()}, &graph));
  AOTEffectPlan plan;
  ASSERT_TRUE(AOTEffectDecomposer::Decompose(graph, &plan));
  ASSERT_EQ(plan.passes.size(), 1u);
  EXPECT_EQ(plan.passes[0].kernel, AOTKernelKind::PerlinNoiseFill);
  EXPECT_EQ(plan.passes[0].nodes, std::vector<AOTNodeID>({AOTNodeID(1), AOTNodeID(2)}));
  EXPECT_FALSE(plan.passes[0].materializesOutput);
  EXPECT_TRUE(AOTPlanExecutor::CanExecute(graph, plan));
}

TGFX_TEST(AOTEffectTest, RectCoverageFoldsIntoPointwiseChain) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  BlockAllocator allocator;
  auto texture = MakeTextureProcessor(context, &allocator, PixelFormat::RGBA_8888);
  ASSERT_NE(texture, nullptr);
  auto rectCoverage = RectEffect::Make(&allocator, Rect::MakeLTRB(10.5f, 10.5f, 90.5f, 90.5f));
  ASSERT_NE(rectCoverage, nullptr);

  AOTEffectGraph graph;
  ASSERT_TRUE(AOTEffectDecomposer::Lower({texture.get(), rectCoverage.get()}, &graph));
  ASSERT_EQ(graph.nodeCount(), 3u);
  auto node = graph.nodeAt(AOTNodeID(2));
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(node->kind, AOTEffectKind::RectCoverage);
  auto parameters = std::get_if<AOTRectCoverageParameters>(&node->parameters);
  ASSERT_NE(parameters, nullptr);
  EXPECT_FLOAT_EQ(parameters->rect[0], 10.5f);
  EXPECT_FLOAT_EQ(parameters->rect[2], 90.5f);

  AOTEffectPlan plan;
  ASSERT_TRUE(AOTEffectDecomposer::Decompose(graph, &plan));
  ASSERT_EQ(plan.passes.size(), 1u);
  EXPECT_EQ(plan.passes[0].kernel, AOTKernelKind::PointwiseChain);
  EXPECT_TRUE(AOTPlanExecutor::CanExecute(graph, plan));
}

TGFX_TEST(AOTEffectTest, ChainedRectCoverageFeedsFromUnitCoverage) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  BlockAllocator allocator;
  auto texture = MakeTextureProcessor(context, &allocator, PixelFormat::RGBA_8888);
  auto colorMatrix = ColorMatrixFragmentProcessor::Make(&allocator, IdentityColorMatrix);
  ASSERT_NE(texture, nullptr);
  ASSERT_NE(colorMatrix, nullptr);
  // A flat two-leaf analytic coverage Compose (device-space AA rect, then a local-space rect):
  // LowerAnalyticCoverageFP lowers it as unit -> rect -> rect, so the first rect node consumes
  // the coverage unit itself while the second is the coverage root.
  auto deviceRect = RectEffect::Make(&allocator, Rect::MakeLTRB(10.5f, 10.5f, 90.5f, 90.5f));
  auto localRect =
      RectEffect::Make(&allocator, Rect::MakeLTRB(20, 20, 80, 80), Matrix::MakeTrans(4, 4));
  auto coverage =
      FragmentProcessor::Compose(&allocator, std::move(deviceRect), std::move(localRect));
  ASSERT_NE(coverage, nullptr);

  AOTEffectGraph graph;
  ASSERT_TRUE(AOTEffectDecomposer::Lower({texture.get(), colorMatrix.get()}, &graph));
  AOTEffectPlan plan;
  ASSERT_TRUE(AOTEffectDecomposer::Decompose(graph, &plan));
  ASSERT_EQ(plan.passes.size(), 1u);
  auto processor =
      AOTChainBuilder::BuildChainProcessor(&allocator, graph, plan.passes[0], {coverage.get()});
  ASSERT_NE(processor, nullptr);
  auto chain = static_cast<const AOTPointwiseChainProcessor*>(processor.get());
  // Slots: [texture, colorMatrix, first rect, second rect(coverage root)]. The first analytic
  // node must read the true GP coverage (the -3 unit designator): the kernel replaces the
  // plain vCoverage modulation with the coverage root's value, so a chain that starts from
  // opaque white (-4) instead of the unit silently drops the GP coverage — a GeometryCoverage
  // value must not depend on which node consumes it.
  ASSERT_GT(chain->slotCount(), 3u);
  EXPECT_EQ(chain->slot(2).op, AOTChainOp::AARectCoverage);
  EXPECT_EQ(chain->slot(3).op, AOTChainOp::LocalRectCoverage);
  EXPECT_EQ(chain->slot(2).in0, -3);
}

TGFX_TEST(AOTEffectTest, WhiteInputFeedsTwoChildBlendChildren) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  BlockAllocator allocator;
  // A single-child DstChild xfer (the SrcIn wrap ShaderMaskFilter produces) lowers its child
  // from the explicit white input; the child here is itself a two-child blend, whose lowering
  // previously rejected the white input — the D3 expression gap. Against white both the
  // children's input (vec4(inputColor.rgb, 1.0) == white) and the epilogue's input-alpha
  // multiply are the identity, so the whole tree must lower and fuse.
  auto childBlend = XfermodeFragmentProcessor::MakeFromTwoProcessors(
      &allocator, MakeTextureProcessor(context, &allocator, PixelFormat::RGBA_8888),
      MakeTextureProcessor(context, &allocator, PixelFormat::RGBA_8888), BlendMode::Multiply);
  ASSERT_NE(childBlend, nullptr);
  auto maskWrap = XfermodeFragmentProcessor::MakeFromDstProcessor(&allocator, std::move(childBlend),
                                                                  BlendMode::SrcIn);
  ASSERT_NE(maskWrap, nullptr);

  AOTEffectGraph graph;
  ASSERT_TRUE(AOTEffectDecomposer::Lower({maskWrap.get()}, &graph));
  AOTEffectPlan plan;
  ASSERT_TRUE(AOTEffectDecomposer::Decompose(graph, &plan));
  ASSERT_EQ(plan.passes.size(), 1u);
  EXPECT_TRUE(AOTPlanExecutor::CanExecute(graph, plan));
  EXPECT_NE(AOTChainBuilder::BuildChainProcessor(&allocator, graph, plan.passes[0]), nullptr);
}

// Counterexample audit D4: a texture whose runtime input is a computed value — an alpha-only
// mask followed by an image-shader texture (the drawImage(A8) + paint.shader shape). The
// sequential lowering feeds the first texture's node into the second as its input, so the
// second texture's modulation source is a computed register, not a designator. The kernel's
// sampling slot must stay in the leading block (static sampler binding) while its modulation
// follows topological order, which the OP_TEX_MODULATE instruction expresses.
TGFX_TEST(AOTEffectTest, TextureConsumingComputedInputLowersToChain) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  BlockAllocator allocator;
  auto maskTexture = MakeTextureProcessor(context, &allocator, PixelFormat::ALPHA_8);
  auto imageTexture = MakeTextureProcessor(context, &allocator, PixelFormat::RGBA_8888);
  ASSERT_NE(maskTexture, nullptr);
  ASSERT_NE(imageTexture, nullptr);

  AOTEffectGraph graph;
  ASSERT_TRUE(AOTEffectDecomposer::Lower({maskTexture.get(), imageTexture.get()}, &graph));
  AOTEffectPlan plan;
  ASSERT_TRUE(AOTEffectDecomposer::Decompose(graph, &plan));
  ASSERT_EQ(plan.passes.size(), 1u);
  EXPECT_TRUE(AOTPlanExecutor::CanExecute(graph, plan));
  auto processor = AOTChainBuilder::BuildChainProcessor(&allocator, graph, plan.passes[0]);
  ASSERT_NE(processor, nullptr);
  auto chain = static_cast<const AOTPointwiseChainProcessor*>(processor.get());
  // Slots: [mask texture (alpha-only color root: bit1 splat + full-input modulate, P1.1
  // semantics), image texture (raw sampling, computed input), TEX_MODULATE]. The modulate
  // instruction reads the image leaf's prefetched sample and the mask slot's result register:
  // the RGBA readback is sample * input.a, in topological order after the producer.
  ASSERT_GE(chain->slotCount(), 3u);
  EXPECT_EQ(chain->slot(0).op, AOTChainOp::Texture);
  EXPECT_EQ(chain->slot(0).textureAlphaOnly, 1);
  EXPECT_EQ(chain->slot(0).textureModulateFullInput, 1);
  EXPECT_EQ(chain->slot(1).op, AOTChainOp::Texture);
  EXPECT_EQ(chain->slot(1).textureModulate, 0);
  EXPECT_EQ(chain->slot(1).textureAlphaOnly, 0);
  EXPECT_EQ(chain->slot(2).op, AOTChainOp::TexModulate);
  EXPECT_EQ(chain->slot(2).texModulateSourceSlot, 1);
  EXPECT_EQ(chain->slot(2).texModulateAlphaOnly, 0);
  EXPECT_EQ(chain->slot(2).in0, chain->slot(0).outRegister);
}

// Counterexample audit P2.3: a two-child xfer whose input is a computed node — a mask texture
// followed by a two-child blend (the drawImage(A8) + paint.shader=BlendShader shape). The
// runtime feeds the children vec4(C.rgb, 1.0) and re-multiplies the blend output by C.a; the
// lowering expresses both halves explicitly: an InputOpaque node for the children's
// environment (which itself is a computed input to the child textures, exercising the
// TEX_MODULATE path) and a MulAlpha instruction for the epilogue.
TGFX_TEST(AOTEffectTest, TwoChildBlendOverComputedInputLowers) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  BlockAllocator allocator;
  auto maskTexture = MakeTextureProcessor(context, &allocator, PixelFormat::ALPHA_8);
  auto textureA = MakeTextureProcessor(context, &allocator, PixelFormat::RGBA_8888);
  auto textureB = MakeTextureProcessor(context, &allocator, PixelFormat::RGBA_8888);
  ASSERT_NE(maskTexture, nullptr);
  ASSERT_NE(textureA, nullptr);
  ASSERT_NE(textureB, nullptr);
  auto blend = XfermodeFragmentProcessor::MakeFromTwoProcessors(
      &allocator, std::move(textureA), std::move(textureB), BlendMode::Multiply);
  ASSERT_NE(blend, nullptr);

  AOTEffectGraph graph;
  ASSERT_TRUE(AOTEffectDecomposer::Lower({maskTexture.get(), blend.get()}, &graph));
  AOTEffectPlan plan;
  ASSERT_TRUE(AOTEffectDecomposer::Decompose(graph, &plan));
  ASSERT_EQ(plan.passes.size(), 1u);
  EXPECT_TRUE(AOTPlanExecutor::CanExecute(graph, plan));
  auto processor = AOTChainBuilder::BuildChainProcessor(&allocator, graph, plan.passes[0]);
  ASSERT_NE(processor, nullptr);
  auto chain = static_cast<const AOTPointwiseChainProcessor*>(processor.get());
  // The mask leaf is an alpha-only color root; the two child textures sample raw and modulate
  // through TEX_MODULATE over the InputOpaque node; the blend carries no selector alpha bit;
  // the MulAlpha epilogue reads the blend result and the mask's register.
  int inputOpaqueCount = 0;
  int mulAlphaCount = 0;
  int texModulateCount = 0;
  int maskRegister = -1;
  for (size_t index = 0; index < chain->slotCount(); ++index) {
    const auto& slot = chain->slot(index);
    if (slot.op == AOTChainOp::Texture && slot.textureAlphaOnly != 0) {
      maskRegister = slot.outRegister;
    }
    if (slot.op == AOTChainOp::InputOpaque) {
      ++inputOpaqueCount;
      EXPECT_EQ(slot.in0, maskRegister);
    }
    if (slot.op == AOTChainOp::MulAlpha) {
      ++mulAlphaCount;
      EXPECT_EQ(slot.in1, maskRegister);
    }
    if (slot.op == AOTChainOp::TexModulate) {
      ++texModulateCount;
      EXPECT_EQ(slot.texModulateAlphaOnly, 0);
    }
    if (slot.op == AOTChainOp::Blend) {
      EXPECT_EQ(slot.blend.multiplyInputAlpha, 0);
    }
  }
  EXPECT_EQ(inputOpaqueCount, 1);
  EXPECT_EQ(mulAlphaCount, 1);
  EXPECT_EQ(texModulateCount, 2);
  EXPECT_GE(maskRegister, 0);
}

TGFX_TEST(AOTEffectTest, PerlinNoisePlusTwoOpsFusesToSinglePass) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  BlockAllocator allocator;
  auto perlin = MakePerlinProcessor(context);
  ASSERT_NE(perlin, nullptr);
  auto colorMatrix = ColorMatrixFragmentProcessor::Make(&allocator, IdentityColorMatrix);
  auto alphaThreshold = AlphaThresholdFragmentProcessor::Make(&allocator, 0.5f);
  ASSERT_NE(colorMatrix, nullptr);
  ASSERT_NE(alphaThreshold, nullptr);

  AOTEffectGraph graph;
  ASSERT_TRUE(
      AOTEffectDecomposer::Lower({perlin.get(), colorMatrix.get(), alphaThreshold.get()}, &graph));
  AOTEffectPlan plan;
  ASSERT_TRUE(AOTEffectDecomposer::Decompose(graph, &plan));
  ASSERT_EQ(plan.passes.size(), 1u);
  EXPECT_EQ(plan.passes[0].kernel, AOTKernelKind::PerlinNoiseFill);
  EXPECT_EQ(plan.passes[0].nodes,
            std::vector<AOTNodeID>({AOTNodeID(1), AOTNodeID(2), AOTNodeID(3)}));
  EXPECT_FALSE(plan.passes[0].materializesOutput);
  EXPECT_TRUE(AOTPlanExecutor::CanExecute(graph, plan));
}

}  // namespace tgfx

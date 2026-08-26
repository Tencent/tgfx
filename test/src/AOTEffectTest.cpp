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
#include "gpu/AOTEffect.h"
#include "gpu/AOTEffectDecomposer.h"
#include "gpu/AOTMaterializationPolicy.h"
#include "gpu/AOTPlanExecutor.h"
#include "gpu/ProgramInfo.h"
#include "gpu/ProxyProvider.h"
#include "gpu/processors/AlphaThresholdFragmentProcessor.h"
#include "gpu/processors/ColorMatrixFragmentProcessor.h"
#include "gpu/processors/ConstColorProcessor.h"
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
    ASSERT_TRUE(AOTEffectDecomposer::Decompose(graph, &plan));
    ASSERT_EQ(plan.passes.size(), 1u);
    EXPECT_EQ(plan.passes[0].kernel, AOTKernelKind::PointwiseChain);
    // The chain kernel's tiled path covers the single-tap wrap modes (Clamp, Repeat*None,
    // MirrorRepeat, ClampToBorder*); only the mipmap-repeat modes (4,5) stay on the plain route.
    bool chainCompatible =
        modeCase.shaderModeX != TiledTextureEffect::ShaderMode::RepeatLinearMipmap &&
        modeCase.shaderModeX != TiledTextureEffect::ShaderMode::RepeatNearestMipmap;
    EXPECT_EQ(AOTPlanExecutor::CanExecute(graph, plan), chainCompatible);
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
  ASSERT_TRUE(AOTEffectDecomposer::Decompose(graph, &plan));
  ASSERT_EQ(plan.passes.size(), 1u);
  EXPECT_EQ(plan.passes[0].kernel, AOTKernelKind::PointwiseChain);
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
  EXPECT_EQ(plan.passes[0].kernel, AOTKernelKind::PointwiseTail);
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
  EXPECT_EQ(fusedPlan.passes[0].kernel, AOTKernelKind::PointwiseTail);
  EXPECT_EQ(fusedPlan.passes[0].nodes,
            std::vector<AOTNodeID>({AOTNodeID(1), AOTNodeID(2), AOTNodeID(3)}));
  EXPECT_FALSE(fusedPlan.passes[0].materializesOutput);
  EXPECT_TRUE(fusedPlan.passes[0].dependencies.empty());
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
  // node0=geometry, node1=texture(dst), node2=blend(src=geometry, dst=texture).
  ASSERT_EQ(graph.nodeCount(), 3u);
  auto blend = graph.nodeAt(AOTNodeID(2));
  ASSERT_NE(blend, nullptr);
  EXPECT_EQ(blend->kind, AOTEffectKind::Blend);
  ASSERT_EQ(blend->inputs.size(), 2u);
  EXPECT_EQ(blend->inputs[0], AOTNodeID(0));  // src = input color
  EXPECT_EQ(blend->inputs[1], AOTNodeID(1));  // dst = child texture
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
  // blends, so the fused-pass sampler budget is the only thing under test.
  auto makeBlendGraph = [&](int textureCount, AOTEffectGraph* graph) {
    AOTNodeBuilder builder = {};
    AOTNodeID geometry = AOTNodeID::Invalid();
    if (!builder.addGeometryColor(&geometry)) {
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
      if (!builder.addTextureSource(index == 0 ? geometry : current, textureParams, &texture)) {
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

  AOTEffectGraph acceptedGraph;
  ASSERT_TRUE(makeBlendGraph(MaxFusedAOTSamplers, &acceptedGraph));
  AOTEffectPlan acceptedPlan;
  EXPECT_TRUE(AOTEffectDecomposer::Decompose(acceptedGraph, &acceptedPlan));

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
  EXPECT_TRUE(AOTPlanExecutor::BuildChainProcessor(&allocator, graph, plan.passes[0]) != nullptr);
}

TGFX_TEST(AOTEffectTest, LinearTextureChainUsesPointwiseTailPlanner) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  BlockAllocator allocator;
  AOTEffectGraph graph;
  ASSERT_TRUE(BuildTripleGraph(context, &allocator, &graph));
  AOTEffectPlan plan;
  ASSERT_TRUE(AOTEffectDecomposer::Decompose(graph, &plan));
  ASSERT_EQ(plan.passes.size(), 1u);
  EXPECT_EQ(plan.passes[0].kernel, AOTKernelKind::PointwiseTail);
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

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
#include <functional>
#include <limits>
#include <set>
#include "base/TGFXTest.h"
#include "core/ClipStack.h"
#include "core/utils/BlockAllocator.h"
#include "gpu/AOTChainBuilder.h"
#include "gpu/AOTEffect.h"
#include "gpu/AOTEffectDecomposer.h"
#include "gpu/AOTMaterializationPolicy.h"
#include "gpu/AOTPlanExecutor.h"
#include "gpu/DrawingManager.h"
#include "gpu/OpsCompositor.h"
#include "gpu/ProgramInfo.h"
#include "gpu/ProxyProvider.h"
#include "gpu/ops/StencilCoverPathDrawOp.h"
#include "gpu/processors/AOTPointwiseChainProcessor.h"
#include "gpu/processors/AlphaThresholdFragmentProcessor.h"
#include "gpu/processors/ClampedGradientEffect.h"
#include "gpu/processors/ColorMatrixFragmentProcessor.h"
#include "gpu/processors/ColorSpaceXFormEffect.h"
#include "gpu/processors/ConstColorProcessor.h"
#include "gpu/processors/DeviceSpaceTextureEffect.h"
#include "gpu/processors/LumaFragmentProcessor.h"
#include "gpu/processors/PerlinNoiseFragmentProcessor.h"
#include "gpu/processors/RRectEffect.h"
#include "gpu/processors/RadialGradientLayout.h"
#include "gpu/processors/RectEffect.h"
#include "gpu/processors/SingleIntervalGradientColorizer.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/processors/TiledTextureEffect.h"
#include "gpu/processors/XfermodeFragmentProcessor.h"
#include "gpu/resources/TextureView.h"
#include "gpu/shaders/PermutationRules.h"
#include "gpu/shaders/level1/PointwiseChainShader.h"
#include "gpu/shaders/level1/PointwiseGraphShader.h"
#include "gtest/gtest.h"
#include "layers/NoiseDensityUtils.h"
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

class SelfManagedPipelineDrawOp : public DrawOp {
 public:
  SelfManagedPipelineDrawOp(BlockAllocator* allocator, bool coverage)
      : DrawOp(allocator, AAType::None), coverage(coverage) {
  }

  bool hasCoverage() const override {
    return coverage;
  }

  Type type() const override {
    return Type::StencilCoverPathDrawOp;
  }

  void execute(RenderPass*, RenderTarget*) override {
    ADD_FAILURE() << "A self-managed draw must not execute through an AOT plan task";
  }

 private:
  bool coverage = false;
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

TGFX_TEST_PRIVATE(AOTEffectTest, ExecutorRejectsSelfManagedDrawWithoutSideEffects) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto* manager = context->drawingManager();
  auto* allocator = manager->drawingAllocator();
  auto destination = RenderTargetProxy::Make(context, 64, 64, false);
  ASSERT_NE(destination, nullptr);
  AOTEffectGraph graph;
  ASSERT_TRUE(BuildTripleGraph(context, allocator, &graph));
  AOTEffectPlan plan;
  ASSERT_TRUE(AOTEffectDecomposer::Decompose(graph, &plan));
  ASSERT_TRUE(AOTPlanExecutor::CanExecute(graph, plan));
  for (bool coverage : {false, true}) {
    SCOPED_TRACE(coverage);
    PlacementPtr<DrawOp> draw = allocator->make<SelfManagedPipelineDrawOp>(allocator, coverage);
    auto color = ConstColorProcessor::Make(allocator, PMColor::White(), InputMode::Ignore);
    ASSERT_NE(color, nullptr);
    auto* originalColor = color.get();
    draw->addColorFP(std::move(color));
    auto clip = RectEffect::Make(allocator, Rect::MakeLTRB(4, 4, 60, 60));
    ASSERT_NE(clip, nullptr);
    auto* originalClip = clip.get();
    draw->addCoverageFP(std::move(clip));
    auto* original = draw.get();
    auto allocated = allocator->size();
    auto* buffer = manager->getDrawingBuffer();
    auto renderTasks = buffer->renderTasks.size();
    auto resourceTasks = buffer->resourceTasks.size();
    auto task =
        AOTPlanExecutor::Make(context, 0, graph, plan, Rect::MakeWH(64, 64), destination, &draw);
    EXPECT_EQ(task, nullptr);
    EXPECT_EQ(draw.get(), original);
    ASSERT_EQ(draw->colorProcessors().size(), 1u);
    EXPECT_EQ(draw->colorProcessors()[0].get(), originalColor);
    ASSERT_EQ(draw->coverages.size(), 1u);
    EXPECT_EQ(draw->coverages[0].get(), originalClip);
    EXPECT_EQ(allocator->size(), allocated);
    EXPECT_EQ(buffer->renderTasks.size(), renderTasks);
    EXPECT_EQ(buffer->resourceTasks.size(), resourceTasks);
  }
  auto color = ConstColorProcessor::Make(allocator, PMColor::White(), InputMode::Ignore);
  auto draw = manager->makeFillDrawOp(destination, std::move(color), 0);
  ASSERT_NE(draw, nullptr);
  auto task =
      AOTPlanExecutor::Make(context, 0, graph, plan, Rect::MakeWH(64, 64), destination, &draw);
  EXPECT_NE(task, nullptr);
  EXPECT_EQ(draw, nullptr);
}

TGFX_TEST_PRIVATE(AOTEffectTest, ExecutorRejectsStencilCoverDrawWithoutTakingOwnership) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto* manager = context->drawingManager();
  auto* allocator = manager->drawingAllocator();
  auto bounds = Rect::MakeWH(64, 64);
  auto destination = RenderTargetProxy::Make(context, 64, 64, false);
  ASSERT_NE(destination, nullptr);
  AOTEffectGraph graph;
  ASSERT_TRUE(BuildTripleGraph(context, allocator, &graph));
  AOTEffectPlan plan;
  ASSERT_TRUE(AOTEffectDecomposer::Decompose(graph, &plan));
  ASSERT_TRUE(AOTPlanExecutor::CanExecute(graph, plan));
  Path path;
  path.addRect(bounds);
  auto geometry = context->proxyProvider()->createStencilCoverPathProxy(Shape::MakeFrom(path), 0);
  ASSERT_NE(geometry, nullptr);
  PlacementPtr<DrawOp> draw = StencilCoverPathDrawOp::Make(geometry, PMColor::White(), Matrix::I(),
                                                           bounds, PathFillType::Winding);
  ASSERT_NE(draw, nullptr);
  EXPECT_EQ(draw->type(), DrawOp::Type::StencilCoverPathDrawOp);
  EXPECT_TRUE(draw->hasCoverage());
  EXPECT_TRUE(draw->needsStencil());
  auto* original = draw.get();
  auto allocated = allocator->size();
  auto* buffer = manager->getDrawingBuffer();
  auto renderTasks = buffer->renderTasks.size();
  auto resourceTasks = buffer->resourceTasks.size();
  auto task =
      AOTPlanExecutor::Make(context, 0, graph, plan, Rect::MakeWH(64, 64), destination, &draw);
  EXPECT_EQ(task, nullptr);
  EXPECT_EQ(draw.get(), original);
  EXPECT_EQ(allocator->size(), allocated);
  EXPECT_EQ(buffer->renderTasks.size(), renderTasks);
  EXPECT_EQ(buffer->resourceTasks.size(), resourceTasks);
}

TGFX_TEST_PRIVATE(AOTEffectTest, CompositorKeepsSelfManagedDrawOutOfFoldAndRetry) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto* cache = context->precompiledShaderCache();
  ASSERT_TRUE(LoadEmbeddedBundle(cache, context->backend()));
  auto* manager = context->drawingManager();
  auto* allocator = manager->drawingAllocator();
  auto destination = RenderTargetProxy::Make(context, 96, 96, false);
  ASSERT_NE(destination, nullptr);
  ClipStack clip;
  clip.clipRect(Rect::MakeLTRB(8.5f, 8.5f, 80.5f, 80.5f), Matrix::I(), true);
  for (bool needsRetry : {false, true}) {
    SCOPED_TRACE(needsRetry);
    OpsCompositor compositor(destination, 0);
    Brush brush(Color::White(), BlendMode::SrcOver, false);
    auto first =
        Shader::MakeLinearGradient(Point(0, 0), Point(96, 96), {Color::Red(), Color::Blue()});
    ASSERT_NE(first, nullptr);
    if (needsRetry) {
      auto second =
          Shader::MakeLinearGradient(Point(96, 0), Point(0, 96), {Color::Green(), Color::White()});
      ASSERT_NE(second, nullptr);
      brush.shader = Shader::MakeBlend(BlendMode::Multiply, first, second);
    } else {
      brush.shader = first;
    }
    ASSERT_NE(brush.shader, nullptr);
    auto applied = compositor.applyClip(clip);
    ASSERT_NE(applied.coverageFP, nullptr);
    auto color = FragmentProcessor::Make(brush.shader, FPArgs(context, 0, Rect::MakeWH(96, 96)));
    ASSERT_NE(color, nullptr);
    AOTEffectGraph graph;
    bool lowered = AOTEffectDecomposer::Lower({color.get(), applied.coverageFP.get()}, &graph);
    AOTEffectPlan plan;
    bool decomposed = lowered && AOTEffectDecomposer::Decompose(graph, &plan);
    EXPECT_EQ(decomposed, !needsRetry);
    PlacementPtr<DrawOp> draw = allocator->make<SelfManagedPipelineDrawOp>(allocator, true);
    auto* original = draw.get();
    auto* buffer = manager->getDrawingBuffer();
    auto renderTasks = buffer->renderTasks.size();
    compositor.addDrawOp(std::move(draw), clip, brush, Rect::MakeWH(96, 96), Rect::MakeWH(96, 96),
                         1);
    ASSERT_EQ(compositor.drawOps.size(), 1u);
    EXPECT_EQ(compositor.drawOps[0].get(), original);
    EXPECT_EQ(compositor.drawOps[0]->type(), DrawOp::Type::StencilCoverPathDrawOp);
    EXPECT_EQ(compositor.drawOps[0]->colorProcessors().size(), 1u);
    EXPECT_EQ(compositor.drawOps[0]->coverages.size(), 1u);
    EXPECT_EQ(buffer->renderTasks.size(), renderTasks);
  }
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
  // The alpha layout decides the texture modulation (RGBA sources scale by the input alpha,
  // alpha-only sources by the whole input), so the lowering must record it per leaf.
  auto rgbaParameters = std::get_if<AOTTextureParameters>(&graph.nodeAt(rgbaNode)->parameters);
  auto alphaParameters = std::get_if<AOTTextureParameters>(&graph.nodeAt(alphaNode)->parameters);
  ASSERT_NE(rgbaParameters, nullptr);
  ASSERT_NE(alphaParameters, nullptr);
  EXPECT_FALSE(rgbaParameters->isAlphaOnly);
  EXPECT_TRUE(alphaParameters->isAlphaOnly);
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
  auto wrongFamily = plan;
  wrongFamily.passes[0].kernel = AOTKernelKind::PointwiseGraph;
  EXPECT_FALSE(AOTPlanExecutor::CanExecute(graph, wrongFamily));
  EXPECT_EQ(AOTChainBuilder::BuildChainProcessor(&allocator, graph, wrongFamily.passes[0]),
            nullptr);
  EXPECT_EQ(AOTChainBuilder::BuildFPForPass(&allocator, graph, wrongFamily.passes[0]), nullptr);
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

TGFX_TEST(AOTEffectTest, ChainSharesTiledBlockBetweenIdenticalLeaves) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  BlockAllocator allocator;
  auto sampleArea = Rect::MakeXYWH(1, 1, 6, 4);
  // Decal over a strict subset resolves to ClampToBorderLinear: both leaves are individually
  // chain-compatible and their recipes are identical, so the pair fuses through
  // TiledLeafIndex/TiledLeafIndex2 (each selector carrying the same recipe) instead of falling back
  // to materialization.
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
  EXPECT_TRUE(AOTEffectDecomposer::Decompose(graph, &plan));
  // Decompose already ran CanExecute on the single fused pass it built; assert it directly so the
  // shared-recipe admission stays pinned independently of the planner's internal call.
  EXPECT_TRUE(AOTPlanExecutor::CanExecute(graph, plan));
}

TGFX_TEST(AOTEffectTest, ChainKeepsIndependentRecipesForTwoTiledLeaves) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  BlockAllocator allocator;
  // Two individually chain-compatible tiled leaves whose recipes differ must fuse into one chain
  // pass, each leaf reading its own recipe: (a) different modes (Decal vs Repeat on X), and (b)
  // the drop-shadow shape — the same Decal mode over different sample areas, so only the subset
  // and clamp rects differ. Sharing one recipe would clamp the second leaf to the first's domain.
  struct RecipeCase {
    TileMode tileModeX2;
    Rect sampleArea2;
  };
  auto sampleArea = Rect::MakeXYWH(1, 1, 6, 4);
  const RecipeCase cases[] = {{TileMode::Repeat, sampleArea},
                              {TileMode::Decal, Rect::MakeXYWH(2, 1, 5, 3)}};
  for (const auto& recipeCase : cases) {
    auto src =
        MakeTiledTextureProcessor(context, &allocator, TileMode::Decal, TileMode::Clamp,
                                  PixelFormat::RGBA_8888, SrcRectConstraint::Strict, sampleArea);
    auto dst = MakeTiledTextureProcessor(context, &allocator, recipeCase.tileModeX2,
                                         TileMode::Clamp, PixelFormat::RGBA_8888,
                                         SrcRectConstraint::Strict, recipeCase.sampleArea2);
    ASSERT_NE(src, nullptr);
    ASSERT_NE(dst, nullptr);
    auto srcRecipe = static_cast<TiledTextureEffect*>(src.get())->resolveSampling();
    auto dstRecipe = static_cast<TiledTextureEffect*>(dst.get())->resolveSampling();
    ASSERT_NE(srcRecipe, nullptr);
    ASSERT_NE(dstRecipe, nullptr);
    ASSERT_TRUE(srcRecipe->shaderModeX != dstRecipe->shaderModeX ||
                srcRecipe->shaderClamp != dstRecipe->shaderClamp);
    auto blend = XfermodeFragmentProcessor::MakeFromTwoProcessors(
        &allocator, std::move(src), std::move(dst), BlendMode::SrcOver);
    ASSERT_NE(blend, nullptr);

    AOTEffectGraph graph;
    ASSERT_TRUE(AOTEffectDecomposer::Lower({blend.get()}, &graph));
    AOTEffectPlan plan;
    ASSERT_TRUE(AOTEffectDecomposer::Decompose(graph, &plan));
    ASSERT_EQ(plan.passes.size(), 1u);
    EXPECT_EQ(plan.passes[0].kernel, AOTKernelKind::PointwiseChain);
    EXPECT_TRUE(AOTPlanExecutor::CanExecute(graph, plan));
    auto processor = AOTChainBuilder::BuildChainProcessor(&allocator, graph, plan.passes[0]);
    ASSERT_NE(processor, nullptr);
    auto chain = static_cast<const AOTPointwiseChainProcessor*>(processor.get());
    ASSERT_GE(chain->tiledLeaf(), 0);
    ASSERT_GE(chain->tiledLeaf2(), 0);
    EXPECT_NE(chain->tiledLeaf(), chain->tiledLeaf2());
    // Whichever leaf lands on which selector, the two selectors carry the two source recipes.
    auto matches = [](const AOTTiledTextureRecipe& recipe, const TiledTextureSampling& source) {
      return recipe.shaderModeX == source.shaderModeX && recipe.shaderModeY == source.shaderModeY &&
             recipe.shaderSubset == source.shaderSubset && recipe.shaderClamp == source.shaderClamp;
    };
    const auto& first = chain->tiledRecipe();
    const auto& second = chain->tiledRecipe2();
    EXPECT_TRUE((matches(first, *srcRecipe) && matches(second, *dstRecipe)) ||
                (matches(first, *dstRecipe) && matches(second, *srcRecipe)));
  }
}

TGFX_TEST(AOTEffectTest, ThreeTiledLeavesUseGraphAndFourthIsRejected) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  BlockAllocator allocator;
  auto sampleArea = Rect::MakeXYWH(1, 1, 6, 4);
  // Three independent tiled leaves require the extended profile, not an RGBA8 intermediate.
  auto makeLeaf = [&]() {
    return MakeTiledTextureProcessor(context, &allocator, TileMode::Decal, TileMode::Clamp,
                                     PixelFormat::RGBA_8888, SrcRectConstraint::Strict, sampleArea);
  };
  auto first = makeLeaf();
  auto second = makeLeaf();
  auto third = makeLeaf();
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);
  ASSERT_NE(third, nullptr);
  auto inner = XfermodeFragmentProcessor::MakeFromTwoProcessors(
      &allocator, std::move(first), std::move(second), BlendMode::SrcOver);
  ASSERT_NE(inner, nullptr);
  auto outer = XfermodeFragmentProcessor::MakeFromTwoProcessors(
      &allocator, std::move(inner), std::move(third), BlendMode::SrcOver);
  ASSERT_NE(outer, nullptr);

  AOTEffectGraph graph;
  ASSERT_TRUE(AOTEffectDecomposer::Lower({outer.get()}, &graph));
  AOTEffectPlan plan;
  ASSERT_TRUE(AOTEffectDecomposer::Decompose(graph, &plan));
  ASSERT_EQ(plan.passes.size(), 1u);
  EXPECT_EQ(plan.passes[0].kernel, AOTKernelKind::PointwiseGraph);
  EXPECT_EQ(plan.passes[0].resourceProfile, AOTResourceProfile::TiledGraph);
  EXPECT_TRUE(AOTPlanExecutor::CanExecute(graph, plan));
  auto rebuilt = AOTChainBuilder::BuildChainProcessor(&allocator, graph, plan.passes[0]);
  ASSERT_NE(rebuilt, nullptr);
  const auto* chain = static_cast<const AOTPointwiseChainProcessor*>(rebuilt.get());
  EXPECT_EQ(chain->resourceProfile(), AOTResourceProfile::TiledGraph);
  EXPECT_EQ(chain->tiledLeafParameters().size(), 3u);
  auto wrongProfile = plan;
  wrongProfile.passes[0].kernel = AOTKernelKind::PointwiseChain;
  wrongProfile.passes[0].resourceProfile = AOTResourceProfile::Chain;
  EXPECT_FALSE(AOTPlanExecutor::CanExecute(graph, wrongProfile));
  auto rect = RectEffect::Make(&allocator, Rect::MakeLTRB(1.5f, 1.5f, 6.5f, 4.5f));
  ASSERT_NE(rect, nullptr);
  EXPECT_EQ(
      AOTChainBuilder::BuildChainProcessor(&allocator, graph, wrongProfile.passes[0], {rect.get()}),
      nullptr);

  auto fourth = makeLeaf();
  ASSERT_NE(fourth, nullptr);
  auto tooWide = XfermodeFragmentProcessor::MakeFromTwoProcessors(
      &allocator, std::move(outer), std::move(fourth), BlendMode::SrcOver);
  ASSERT_NE(tooWide, nullptr);
  AOTEffectGraph wideGraph;
  ASSERT_TRUE(AOTEffectDecomposer::Lower({tooWide.get()}, &wideGraph));
  AOTEffectPlan widePlan;
  EXPECT_FALSE(AOTEffectDecomposer::Decompose(wideGraph, &widePlan));
}

TGFX_TEST(AOTEffectTest, DirectProcessorRequiresExplicitThreeRecipeProfile) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  BlockAllocator allocator;
  auto tiled = MakeTiledTextureProcessor(context, &allocator, TileMode::Decal, TileMode::Clamp,
                                         PixelFormat::RGBA_8888, SrcRectConstraint::Strict,
                                         Rect::MakeXYWH(1, 1, 6, 4));
  ASSERT_NE(tiled, nullptr);
  auto recipe = static_cast<TiledTextureEffect*>(tiled.get())->resolveSampling();
  ASSERT_NE(recipe, nullptr);
  auto build = [&](AOTResourceProfile profile, int additionalIndex, size_t additionalCount) {
    std::vector<PlacementPtr<FragmentProcessor>> leaves;
    std::vector<AOTChainSlot> slots(4);
    for (int index = 0; index < 4; ++index) {
      auto leaf = MakeTextureProcessor(context, &allocator, PixelFormat::RGBA_8888);
      if (leaf == nullptr) {
        return PlacementPtr<AOTPointwiseChainProcessor>(nullptr);
      }
      leaves.push_back(std::move(leaf));
      slots[static_cast<size_t>(index)].op = AOTChainOp::Texture;
      slots[static_cast<size_t>(index)].outRegister = index;
    }
    std::vector<AOTChainTiledLeaf> extra;
    for (size_t index = 0; index < additionalCount; ++index) {
      extra.push_back({additionalIndex + static_cast<int>(index), *recipe});
    }
    return AOTPointwiseChainProcessor::Make(&allocator, std::move(leaves), slots, 3, 0, recipe,
                                            nullptr, -1, ~0u, nullptr, -1, {}, false, -1, 1, recipe,
                                            profile, extra);
  };
  EXPECT_EQ(build(AOTResourceProfile::Automatic, 2, 1), nullptr);
  EXPECT_EQ(build(AOTResourceProfile::Chain, 2, 1), nullptr);
  EXPECT_NE(build(AOTResourceProfile::TiledGraph, 2, 1), nullptr);
  EXPECT_EQ(build(AOTResourceProfile::TiledGraph, 1, 1), nullptr);
  EXPECT_EQ(build(AOTResourceProfile::TiledGraph, 4, 1), nullptr);
  EXPECT_EQ(build(AOTResourceProfile::TiledGraph, 2, 2), nullptr);
  EXPECT_EQ(build(static_cast<AOTResourceProfile>(99), 2, 1), nullptr);
}

TGFX_TEST(AOTEffectTest, ResourceProfilesRejectInvalidAndOverBudgetDemands) {
  AOTResourceDemand demand = {};
  demand.leafBindings = 3;
  demand.tiledRecipes = 2;
  EXPECT_EQ(SelectAOTResourceProfile(demand), AOTResourceProfile::Chain);
  demand.tiledRecipes = 3;
  EXPECT_EQ(SelectAOTResourceProfile(demand), AOTResourceProfile::TiledGraph);
  EXPECT_FALSE(FitsAOTResourceProfile(demand, AOTResourceProfile::Chain));
  demand.tiledRecipes = 4;
  EXPECT_EQ(SelectAOTResourceProfile(demand), AOTResourceProfile::Automatic);
  demand.tiledRecipes = 0;
  demand.noiseSources = 1;
  EXPECT_EQ(SelectAOTResourceProfile(demand), AOTResourceProfile::NoiseGraph);
  demand.gradients = 1;
  EXPECT_EQ(SelectAOTResourceProfile(demand), AOTResourceProfile::Automatic);
  demand.noiseSources = 0;
  EXPECT_EQ(SelectAOTResourceProfile(demand), AOTResourceProfile::TiledGraph);
  EXPECT_FALSE(FitsAOTResourceProfile(demand, AOTResourceProfile::Chain));
  EXPECT_TRUE(FitsAOTResourceProfile(demand, AOTResourceProfile::TiledGraph));
  demand.gradients = 2;
  EXPECT_EQ(SelectAOTResourceProfile(demand), AOTResourceProfile::Automatic);
  demand = {};
  demand.instructions = 33;
  EXPECT_EQ(SelectAOTResourceProfile(demand), AOTResourceProfile::Automatic);
  demand = {};
  demand.registers = 17;
  EXPECT_EQ(SelectAOTResourceProfile(demand), AOTResourceProfile::Automatic);
  demand = {};
  demand.leafBindings = 5;
  EXPECT_EQ(SelectAOTResourceProfile(demand), AOTResourceProfile::Automatic);
  EXPECT_FALSE(FitsAOTResourceProfile({}, static_cast<AOTResourceProfile>(99)));
}

TGFX_TEST(AOTEffectTest, DirectGradientProcessorRejectsChainAndAutomaticallySelectsGraph) {
  BlockAllocator allocator;
  std::vector<AOTChainSlot> slots(1);
  slots[0].op = AOTChainOp::Gradient;
  slots[0].in0 = -1;
  slots[0].outRegister = 0;
  slots[0].gradient.coordMatrix = Matrix::MakeTrans(4, 6);
  for (auto profile : {AOTResourceProfile::Automatic, AOTResourceProfile::Chain,
                       AOTResourceProfile::TiledGraph, AOTResourceProfile::NoiseGraph}) {
    auto processor =
        AOTPointwiseChainProcessor::Make(&allocator, {}, slots, 0, -1, nullptr, nullptr, -1, ~0u,
                                         nullptr, -1, {}, false, -1, -1, nullptr, profile);
    if (profile == AOTResourceProfile::Chain || profile == AOTResourceProfile::NoiseGraph) {
      EXPECT_EQ(processor, nullptr);
      continue;
    }
    ASSERT_NE(processor, nullptr);
    EXPECT_EQ(processor->resourceProfile(), AOTResourceProfile::TiledGraph);
    ASSERT_EQ(processor->numCoordTransforms(), 1u);
    EXPECT_EQ(processor->coordTransform(0)->matrix, slots[0].gradient.coordMatrix);
  }
}

static PlacementPtr<AOTPointwiseChainProcessor> MakeChainWithTextureLeaves(
    Context* context, BlockAllocator* allocator, size_t leafCount,
    const std::vector<AOTChainSlot>& slots, size_t root, AOTResourceProfile profile) {
  std::vector<PlacementPtr<FragmentProcessor>> leaves;
  std::vector<PlacementPtr<FragmentProcessor>> padding;
  for (size_t index = 0; index < (leafCount == 0 ? 0u : 4u); ++index) {
    auto texture = MakeTextureProcessor(context, allocator, PixelFormat::RGBA_8888);
    if (texture == nullptr) {
      return nullptr;
    }
    if (index < leafCount) {
      leaves.push_back(std::move(texture));
    } else {
      padding.push_back(std::move(texture));
    }
  }
  return AOTPointwiseChainProcessor::Make(allocator, std::move(leaves), slots, root, -1, nullptr,
                                          nullptr, -1, ~0u, nullptr, -1, std::move(padding), false,
                                          -1, -1, nullptr, profile);
}

TGFX_TEST(AOTEffectTest, TextureSlotsMustMatchRealLeaves) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  BlockAllocator allocator;
  for (auto profile : {AOTResourceProfile::Chain, AOTResourceProfile::TiledGraph}) {
    for (size_t leafCount = 0; leafCount <= 4; ++leafCount) {
      SCOPED_TRACE(static_cast<uint32_t>(profile));
      SCOPED_TRACE(leafCount);
      std::vector<AOTChainSlot> slots(leafCount + 1);
      for (size_t index = 0; index < leafCount; ++index) {
        slots[index].op = AOTChainOp::Texture;
      }
      slots.back().op = AOTChainOp::ConstColor;
      slots.back().in0 = -1;
      slots.back().outRegister = 0;
      ASSERT_NE(MakeChainWithTextureLeaves(context, &allocator, leafCount, slots, 0, profile),
                nullptr);
      slots.back().op = AOTChainOp::Texture;
      EXPECT_EQ(MakeChainWithTextureLeaves(context, &allocator, leafCount, slots, 0, profile),
                nullptr);
      if (leafCount > 0) {
        slots.back().op = AOTChainOp::ConstColor;
        slots[0].op = AOTChainOp::ConstColor;
        slots[0].in0 = -1;
        EXPECT_EQ(MakeChainWithTextureLeaves(context, &allocator, leafCount, slots, 0, profile),
                  nullptr);
      }
    }
  }
}

TGFX_TEST(AOTEffectTest, TexModulateRequiresRealTextureSource) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  BlockAllocator allocator;
  for (auto profile : {AOTResourceProfile::Chain, AOTResourceProfile::TiledGraph}) {
    for (size_t leafCount = 0; leafCount <= 4; ++leafCount) {
      SCOPED_TRACE(static_cast<uint32_t>(profile));
      SCOPED_TRACE(leafCount);
      std::vector<AOTChainSlot> slots(leafCount + 1);
      for (size_t index = 0; index < leafCount; ++index) {
        slots[index].op = AOTChainOp::Texture;
      }
      slots.back().op = AOTChainOp::TexModulate;
      slots.back().in0 = -1;
      slots.back().outRegister = 0;
      slots.back().texModulateSourceSlot = static_cast<int>(leafCount);
      EXPECT_EQ(MakeChainWithTextureLeaves(context, &allocator, leafCount, slots, 0, profile),
                nullptr);
      if (leafCount > 0) {
        slots.back().texModulateSourceSlot = static_cast<int>(leafCount - 1);
        ASSERT_NE(MakeChainWithTextureLeaves(context, &allocator, leafCount, slots, 0, profile),
                  nullptr);
      }
    }
  }
}

TGFX_TEST(AOTEffectTest, BaseChainOmitsGradientTransformAndExtendedProfilesKeepOrdinals) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  BlockAllocator allocator;
  for (auto profile : {AOTResourceProfile::Chain, AOTResourceProfile::TiledGraph}) {
    std::vector<PlacementPtr<FragmentProcessor>> leaves;
    std::vector<AOTChainSlot> slots(4);
    std::array<Matrix, 4> matrices;
    for (size_t index = 0; index < slots.size(); ++index) {
      matrices[index] = Matrix::MakeTrans(static_cast<float>(index + 1), 6);
      auto proxy = context->proxyProvider()->createTextureProxy({}, 2, 2, PixelFormat::RGBA_8888);
      ASSERT_NE(proxy, nullptr);
      auto leaf = TextureEffect::Make(&allocator, std::move(proxy), {}, &matrices[index]);
      ASSERT_NE(leaf, nullptr);
      matrices[index] = leaf->coordTransform(0)->matrix;
      leaves.push_back(std::move(leaf));
      slots[index].op = AOTChainOp::Texture;
      slots[index].outRegister = static_cast<int>(index);
    }
    auto processor = AOTPointwiseChainProcessor::Make(&allocator, std::move(leaves), slots, 3, -1,
                                                      nullptr, nullptr, -1, ~0u, nullptr, -1, {},
                                                      false, -1, -1, nullptr, profile);
    ASSERT_NE(processor, nullptr);
    EXPECT_EQ(processor->numCoordTransforms(), profile == AOTResourceProfile::Chain ? 0u : 1u);
    if (profile == AOTResourceProfile::TiledGraph) {
      EXPECT_EQ(processor->coordTransform(0)->matrix, Matrix::I());
    }
    ProgramInfo programInfo(nullptr, nullptr, {processor.get()}, 1, nullptr, BlendMode::SrcOver);
    FragmentProcessor::CoordTransformIter iter(&programInfo);
    if (profile == AOTResourceProfile::TiledGraph) {
      EXPECT_EQ(iter.next(), processor->coordTransform(0));
    }
    for (size_t index = 0; index < slots.size(); ++index) {
      auto transform = iter.next();
      ASSERT_NE(transform, nullptr);
      EXPECT_EQ(transform, processor->childProcessor(index)->coordTransform(0));
      EXPECT_EQ(transform->matrix, matrices[index]);
    }
    EXPECT_EQ(iter.next(), nullptr);
  }
}

TGFX_TEST(AOTEffectTest, DAGAdmissionChecksActualRegisterPressure) {
  AOTNodeBuilder builder;
  AOTNodeID geometry;
  ASSERT_TRUE(builder.addGeometryColor(&geometry));
  std::vector<AOTNodeID> constants;
  for (int index = 0; index < 17; ++index) {
    AOTConstColorParameters params;
    params.color = {0.1f, 0.2f, 0.3f, 1.0f};
    AOTNodeID node;
    ASSERT_TRUE(builder.addConstColor(geometry, params, &node));
    constants.push_back(node);
  }
  auto combine = [&](size_t first, size_t last) {
    AOTNodeID result = constants[first];
    for (size_t index = first + 1; index < last; ++index) {
      AOTBlendParameters blend;
      blend.blendMode = static_cast<int>(BlendMode::SrcOver);
      AOTNodeID node;
      if (!builder.addBlend(result, constants[index], blend, &node)) {
        return AOTNodeID::Invalid();
      }
      result = node;
    }
    return result;
  };
  auto root = combine(0, 9);
  ASSERT_TRUE(root.isValid());
  ASSERT_TRUE(combine(9, 17).isValid());
  AOTEffectGraph graph;
  ASSERT_TRUE(builder.finish(root, &graph));
  AOTEffectPlan plan;
  EXPECT_FALSE(AOTEffectDecomposer::Decompose(graph, &plan));
}

TGFX_TEST(AOTEffectTest, CoverageRecipeCanPromoteChainToGraph) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  BlockAllocator allocator;
  auto sampleArea = Rect::MakeXYWH(1, 1, 6, 4);
  auto makeLeaf = [&]() {
    return MakeTiledTextureProcessor(context, &allocator, TileMode::Decal, TileMode::Clamp,
                                     PixelFormat::RGBA_8888, SrcRectConstraint::Strict, sampleArea);
  };
  auto first = makeLeaf();
  auto second = makeLeaf();
  auto coverage = makeLeaf();
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);
  ASSERT_NE(coverage, nullptr);
  auto blend = XfermodeFragmentProcessor::MakeFromTwoProcessors(
      &allocator, std::move(first), std::move(second), BlendMode::SrcOver);
  ASSERT_NE(blend, nullptr);
  AOTEffectGraph graph;
  ASSERT_TRUE(AOTEffectDecomposer::Lower({blend.get()}, &graph));
  AOTEffectPlan plan;
  ASSERT_TRUE(AOTEffectDecomposer::Decompose(graph, &plan));
  EXPECT_EQ(plan.passes[0].resourceProfile, AOTResourceProfile::Chain);
  auto rebuilt =
      AOTChainBuilder::BuildChainProcessor(&allocator, graph, plan.passes[0], {coverage.get()});
  ASSERT_NE(rebuilt, nullptr);
  const auto* chain = static_cast<const AOTPointwiseChainProcessor*>(rebuilt.get());
  EXPECT_EQ(chain->resourceProfile(), AOTResourceProfile::TiledGraph);
  EXPECT_EQ(chain->tiledLeafParameters().size(), 3u);
  EXPECT_GE(chain->coverageRoot(), 0);
}

TGFX_TEST(AOTEffectTest, CoverageGradientPromotesBaseChainToTiledGraph) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  BlockAllocator allocator;
  auto color = ConstColorProcessor::Make(&allocator, PMColor::White(), InputMode::ModulateRGBA);
  auto colorizer = SingleIntervalGradientColorizer::Make(&allocator, Color::Red(), Color::Blue());
  auto matrix = Matrix::MakeTrans(4, 6);
  auto layout = RadialGradientLayout::Make(&allocator, matrix);
  auto coverage = ClampedGradientEffect::Make(&allocator, std::move(colorizer), std::move(layout),
                                              Color::Red(), Color::Blue());
  ASSERT_NE(color, nullptr);
  ASSERT_NE(coverage, nullptr);
  AOTEffectGraph graph;
  ASSERT_TRUE(AOTEffectDecomposer::Lower({color.get()}, &graph));
  AOTEffectPlan plan;
  ASSERT_TRUE(AOTEffectDecomposer::Decompose(graph, &plan));
  ASSERT_EQ(plan.passes.size(), 1u);
  EXPECT_EQ(plan.passes[0].kernel, AOTKernelKind::PointwiseChain);
  EXPECT_EQ(plan.passes[0].resourceProfile, AOTResourceProfile::Chain);
  auto original = AOTChainBuilder::BuildChainProcessor(&allocator, graph, plan.passes[0]);
  ASSERT_NE(original, nullptr);
  EXPECT_EQ(original->numCoordTransforms(), 0u);
  auto rebuilt =
      AOTChainBuilder::BuildChainProcessor(&allocator, graph, plan.passes[0], {coverage.get()});
  ASSERT_NE(rebuilt, nullptr);
  const auto* chain = static_cast<const AOTPointwiseChainProcessor*>(rebuilt.get());
  EXPECT_EQ(chain->resourceProfile(), AOTResourceProfile::TiledGraph);
  EXPECT_GE(chain->coverageRoot(), 0);
  ASSERT_EQ(chain->numCoordTransforms(), 1u);
  EXPECT_EQ(chain->coordTransform(0)->matrix, matrix);
  size_t gradientCount = 0;
  for (size_t index = 0; index < chain->slotCount(); ++index) {
    if (chain->slot(index).op == AOTChainOp::Gradient) {
      ++gradientCount;
      EXPECT_EQ(chain->slot(index).in0, -3);
    }
  }
  EXPECT_EQ(gradientCount, 1u);
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
}

// A multi-pass linear plan: the source pass, then passes of operators each reading its
// predecessor, every pass but the last materializing, all on the chain kernel.
static void ExpectSegmentedChainPlan(const AOTEffectGraph& graph, const AOTEffectPlan& plan,
                                     size_t passes) {
  ASSERT_EQ(plan.passes.size(), passes);
  for (size_t index = 0; index < passes; ++index) {
    const auto& pass = plan.passes[index];
    EXPECT_EQ(pass.kernel, AOTKernelKind::PointwiseChain);
    EXPECT_EQ(pass.materializesOutput, index + 1 < passes);
    if (index == 0) {
      EXPECT_TRUE(pass.dependencies.empty());
    } else {
      EXPECT_EQ(pass.dependencies, std::vector<uint32_t>{static_cast<uint32_t>(index - 1)});
    }
  }
  EXPECT_EQ(plan.passes.back().output, graph.root());
  EXPECT_TRUE(AOTPlanExecutor::CanExecute(graph, plan));
}

// A device-space texture source is a chain leaf sampled at the fragment position, so a
// device-space linear chain fuses into one chain pass like a plain one. The leaf child is a plain
// TextureEffect, and the chain processor records which leaf samples in device space.
TGFX_TEST(AOTEffectTest, DeviceSpaceLinearChainUsesChainKernel) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  BlockAllocator allocator;
  auto proxy = context->proxyProvider()->createTextureProxy({}, 2, 2, PixelFormat::RGBA_8888);
  ASSERT_NE(proxy, nullptr);
  ASSERT_NE(proxy->getTextureView(), nullptr);
  auto texture = DeviceSpaceTextureEffect::Make(&allocator, proxy, Matrix::MakeTrans(3, 5));
  auto colorMatrix = ColorMatrixFragmentProcessor::Make(&allocator, IdentityColorMatrix);
  auto luma = LumaFragmentProcessor::Make(&allocator);
  auto colorMatrix2 = ColorMatrixFragmentProcessor::Make(&allocator, IdentityColorMatrix);
  ASSERT_NE(texture, nullptr);
  ASSERT_NE(colorMatrix, nullptr);
  ASSERT_NE(luma, nullptr);
  ASSERT_NE(colorMatrix2, nullptr);
  for (const auto& processors : std::vector<std::vector<const FragmentProcessor*>>{
           {texture.get(), colorMatrix.get(), luma.get()},
           {texture.get(), colorMatrix.get(), luma.get(), colorMatrix2.get()}}) {
    SCOPED_TRACE(processors.size());
    AOTEffectGraph graph;
    ASSERT_TRUE(AOTEffectDecomposer::Lower(processors, &graph));
    AOTEffectPlan plan;
    ASSERT_TRUE(AOTEffectDecomposer::Decompose(graph, &plan));
    ASSERT_EQ(plan.passes.size(), 1u);
    EXPECT_EQ(plan.passes[0].kernel, AOTKernelKind::PointwiseChain);
    EXPECT_TRUE(AOTPlanExecutor::CanExecute(graph, plan));
    auto processor = AOTChainBuilder::BuildFPForPass(&allocator, graph, plan.passes[0]);
    ASSERT_NE(processor, nullptr);
    ASSERT_EQ(processor->name(), "AOTPointwiseChainProcessor");
    auto chain = static_cast<const AOTPointwiseChainProcessor*>(processor.get());
    EXPECT_EQ(chain->deviceLeafIndex(), 0);
    EXPECT_EQ(chain->childProcessor(0)->name(), "TextureEffect");
    EXPECT_EQ(AOTEffectDecomposer::AnalyzeChain(processors).outcome,
              AOTDecomposeOutcome::FusablePointwise);
  }

  // The kernel carries one device-space leaf coordinate matrix, so a DAG blending two device-space
  // sources does not fit one chain pass.
  AOTNodeBuilder builder;
  AOTNodeID geometry, first, second, blended;
  ASSERT_TRUE(builder.addGeometryColor(&geometry));
  AOTTextureParameters device = {};
  device.textureProxy = proxy;
  device.samplingKind = AOTTextureSamplingKind::Device;
  ASSERT_TRUE(builder.addTextureSource(geometry, device, &first));
  ASSERT_TRUE(builder.addTextureSource(geometry, device, &second));
  AOTBlendParameters blend = {};
  blend.blendMode = static_cast<int>(BlendMode::SrcOver);
  ASSERT_TRUE(builder.addBlend(first, second, blend, &blended));
  AOTEffectGraph twoDeviceLeaves;
  ASSERT_TRUE(builder.finish(blended, &twoDeviceLeaves));
  AOTEffectPlan refused;
  EXPECT_FALSE(AOTEffectDecomposer::Decompose(twoDeviceLeaves, &refused));
}

// The chain takes an alpha-only device-space source and an RGBAAA plain source as leaves. The
// rebuilt leaf children keep the properties the kernel reads per leaf: the alpha-only proxy (its
// slot selector splat) and the RGBAAA alpha-area offset (AlphaStart / HasRgbaaa).
TGFX_TEST(AOTEffectTest, ChainLeavesTakeAlphaOnlyDeviceAndRgbaaaSources) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  BlockAllocator allocator;
  auto alphaProxy = context->proxyProvider()->createTextureProxy({}, 4, 4, PixelFormat::ALPHA_8);
  ASSERT_NE(alphaProxy, nullptr);
  ASSERT_TRUE(alphaProxy->isAlphaOnly());
  auto rgbaProxy = context->proxyProvider()->createTextureProxy({}, 8, 4, PixelFormat::RGBA_8888);
  ASSERT_NE(rgbaProxy, nullptr);
  auto device = DeviceSpaceTextureEffect::Make(&allocator, alphaProxy, Matrix::I());
  auto rgbaaa = TextureEffect::MakeRGBAAA(&allocator, rgbaProxy, {}, Point::Make(4, 0));
  auto colorMatrix = ColorMatrixFragmentProcessor::Make(&allocator, IdentityColorMatrix);
  ASSERT_NE(device, nullptr);
  ASSERT_NE(rgbaaa, nullptr);
  ASSERT_NE(colorMatrix, nullptr);
  struct Case {
    const FragmentProcessor* source;
    bool device;
  };
  for (const auto& testCase : {Case{device.get(), true}, Case{rgbaaa.get(), false}}) {
    SCOPED_TRACE(testCase.device ? "device-alpha-only" : "rgbaaa");
    std::vector<const FragmentProcessor*> processors = {testCase.source, colorMatrix.get()};
    AOTEffectGraph graph;
    ASSERT_TRUE(AOTEffectDecomposer::Lower(processors, &graph));
    AOTEffectPlan plan;
    ASSERT_TRUE(AOTEffectDecomposer::Decompose(graph, &plan));
    ASSERT_EQ(plan.passes.size(), 1u);
    EXPECT_EQ(plan.passes[0].kernel, AOTKernelKind::PointwiseChain);
    EXPECT_TRUE(AOTPlanExecutor::CanExecute(graph, plan));
    auto processor = AOTChainBuilder::BuildFPForPass(&allocator, graph, plan.passes[0]);
    ASSERT_NE(processor, nullptr);
    ASSERT_EQ(processor->name(), "AOTPointwiseChainProcessor");
    auto chain = static_cast<const AOTPointwiseChainProcessor*>(processor.get());
    EXPECT_EQ(chain->deviceLeafIndex(), testCase.device ? 0 : -1);
    ASSERT_EQ(chain->childProcessor(0)->name(), "TextureEffect");
    auto leaf = static_cast<const TextureEffect*>(chain->childProcessor(0));
    EXPECT_EQ(leaf->isAlphaOnly(), testCase.device);
    EXPECT_EQ(leaf->hasRGBAAA(), !testCase.device);
  }
}

// A multi-pass plan never starts a pass at an AlphaThreshold, because the pass boundary would
// round the threshold's input to RGBA8. A chain pass holds the source plus 15 operators; the
// planner ends a pass early so the threshold joins the operator that feeds it, and when no earlier
// boundary is left (the threshold's feeding operators are thresholds too), the chain is refused.
// CanExecute rejects a hand-built plan that breaks the rule.
TGFX_TEST(AOTEffectTest, MultiPassPlanNeverStartsPassAtThreshold) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  BlockAllocator allocator;
  auto proxy = context->proxyProvider()->createTextureProxy({}, 2, 2, PixelFormat::RGBA_8888);
  ASSERT_NE(proxy, nullptr);
  auto texture = DeviceSpaceTextureEffect::Make(&allocator, std::move(proxy), Matrix::I());
  ASSERT_NE(texture, nullptr);
  constexpr size_t PassOperators = ChainResourceCapacity.instructions - 1;
  std::vector<PlacementPtr<FragmentProcessor>> owners = {};
  auto matrix = [&]() {
    owners.push_back(ColorMatrixFragmentProcessor::Make(&allocator, IdentityColorMatrix));
    return owners.back().get();
  };
  auto threshold = [&]() {
    owners.push_back(AlphaThresholdFragmentProcessor::Make(&allocator, 0.5f));
    return owners.back().get();
  };

  // [matrix x15, threshold, matrix]: the budget alone would split [source, matrix x15]
  // | [threshold, matrix]. The planner gives [source, matrix x14] | [matrix, threshold, matrix].
  std::vector<const FragmentProcessor*> processors = {texture.get()};
  for (size_t index = 0; index < PassOperators; ++index) {
    processors.push_back(matrix());
  }
  processors.push_back(threshold());
  processors.push_back(matrix());
  AOTEffectGraph graph;
  ASSERT_TRUE(AOTEffectDecomposer::Lower(processors, &graph));
  AOTEffectPlan plan;
  ASSERT_TRUE(AOTEffectDecomposer::Decompose(graph, &plan));
  ExpectSegmentedChainPlan(graph, plan, 2);
  EXPECT_EQ(plan.passes[0].nodes.size(), PassOperators);
  ASSERT_EQ(plan.passes[1].nodes.size(), 3u);
  EXPECT_EQ(graph.nodeAt(plan.passes[1].nodes[1])->kind, AOTEffectKind::AlphaThreshold);

  // The plain instruction-budget split of the same chain starts its second pass at the threshold.
  const auto thresholdNode = static_cast<uint32_t>(PassOperators + 2);
  ASSERT_EQ(graph.nodeAt(AOTNodeID(thresholdNode))->kind, AOTEffectKind::AlphaThreshold);
  AOTEffectPlan budgetSplit = plan;
  budgetSplit.passes[0].nodes.clear();
  for (uint32_t index = 1; index < thresholdNode; ++index) {
    budgetSplit.passes[0].nodes.push_back(AOTNodeID(index));
  }
  budgetSplit.passes[0].output = AOTNodeID(thresholdNode - 1);
  budgetSplit.passes[1].nodes = {AOTNodeID(thresholdNode), AOTNodeID(thresholdNode + 1)};
  budgetSplit.passes[1].output = AOTNodeID(thresholdNode + 1);
  EXPECT_FALSE(AOTPlanExecutor::CanExecute(graph, budgetSplit));

  // [matrix, threshold x31]: the operators need two passes, but every boundary inside the chain is
  // in front of a threshold or would leave the second pass with no operator.
  std::vector<const FragmentProcessor*> refusedProcessors = {texture.get(), matrix()};
  for (size_t index = 0; index < PassOperators; ++index) {
    refusedProcessors.push_back(threshold());
  }
  AOTEffectGraph refusedGraph;
  ASSERT_TRUE(AOTEffectDecomposer::Lower(refusedProcessors, &refusedGraph));
  AOTEffectPlan refusedPlan;
  EXPECT_FALSE(AOTEffectDecomposer::Decompose(refusedGraph, &refusedPlan));
}

// CanExecute and the chain builder share one planner, so a pass whose node list omits a producer
// that another pass node consumes is refused at admission rather than promised and then refused
// at construction.
TGFX_TEST(AOTEffectTest, DAGAdmissionRejectsPassMissingProducer) {
  BlockAllocator allocator;
  AOTNodeBuilder builder;
  AOTNodeID geometry, source, root;
  ASSERT_TRUE(builder.addGeometryColor(&geometry));
  AOTConstColorParameters color;
  color.color = {0.2f, 0.3f, 0.4f, 1.0f};
  ASSERT_TRUE(builder.addConstColor(geometry, color, &source));
  ASSERT_TRUE(builder.addColorMatrix(source, {IdentityColorMatrix}, &root));
  AOTEffectGraph graph;
  ASSERT_TRUE(builder.finish(root, &graph));
  AOTEffectPlan plan;
  ASSERT_TRUE(AOTEffectDecomposer::Decompose(graph, &plan));
  ASSERT_EQ(plan.passes.size(), 1u);
  ASSERT_TRUE(AOTPlanExecutor::CanExecute(graph, plan));
  EXPECT_NE(AOTChainBuilder::BuildChainProcessor(&allocator, graph, plan.passes[0]), nullptr);

  auto missingProducer = plan;
  missingProducer.passes[0].nodes = {root};
  EXPECT_FALSE(AOTPlanExecutor::CanExecute(graph, missingProducer));
  EXPECT_EQ(AOTChainBuilder::BuildChainProcessor(&allocator, graph, missingProducer.passes[0]),
            nullptr);

  auto duplicated = plan;
  duplicated.passes[0].nodes.push_back(root);
  EXPECT_FALSE(AOTPlanExecutor::CanExecute(graph, duplicated));
}

TGFX_TEST(AOTEffectTest, TwoColorSpaceTransformsSplitIntoChainPasses) {
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
    // Two transforms exceed the chain kernel's single color-space block, so the multi-pass
    // planner cuts the chain in front of the second one: [source, xform(, luma)] | [xform].
    AOTEffectPlan plan;
    ASSERT_TRUE(AOTEffectDecomposer::Decompose(graph, &plan));
    ExpectSegmentedChainPlan(graph, plan, 2u);
    EXPECT_EQ(plan.passes[0].nodes.size(), insertLuma ? 3u : 2u);
    EXPECT_EQ(plan.passes[1].nodes, std::vector<AOTNodeID>{graph.root()});
    EXPECT_NE(AOTChainBuilder::BuildFPForPass(&allocator, graph, plan.passes[0]), nullptr);
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
      EXPECT_EQ(plan.passes[0].kernel, AOTKernelKind::PointwiseGraph);
      EXPECT_EQ(plan.passes[0].resourceProfile, AOTResourceProfile::TiledGraph);
      EXPECT_TRUE(AOTPlanExecutor::CanExecute(graph, plan));
      BlockAllocator allocator;
      auto rebuilt = AOTChainBuilder::BuildChainProcessor(&allocator, graph, plan.passes[0]);
      ASSERT_NE(rebuilt, nullptr);
      const auto* chain = static_cast<const AOTPointwiseChainProcessor*>(rebuilt.get());
      EXPECT_EQ(chain->resourceProfile(), AOTResourceProfile::TiledGraph);
      EXPECT_EQ(chain->lutLeaf(), textureCount);
      auto maskProxy = context->proxyProvider()->createTextureProxy({}, 2, 2, PixelFormat::ALPHA_8);
      ASSERT_NE(maskProxy, nullptr);
      auto mask = DeviceSpaceTextureEffect::Make(&allocator, std::move(maskProxy), Matrix::I());
      ASSERT_NE(mask, nullptr);
      auto masked =
          AOTChainBuilder::BuildChainProcessor(&allocator, graph, plan.passes[0], {mask.get()});
      ASSERT_NE(masked, nullptr);
      const auto* maskedChain = static_cast<const AOTPointwiseChainProcessor*>(masked.get());
      EXPECT_TRUE(maskedChain->hasMask());
      EXPECT_EQ(maskedChain->resourceProfile(), AOTResourceProfile::TiledGraph);
      EXPECT_EQ(maskedChain->lutLeaf(), textureCount);
      EXPECT_EQ(maskedChain->leafCount(), 4u);
      auto forcedChain = plan;
      forcedChain.passes[0].kernel = AOTKernelKind::PointwiseChain;
      forcedChain.passes[0].resourceProfile = AOTResourceProfile::Chain;
      EXPECT_FALSE(AOTPlanExecutor::CanExecute(graph, forcedChain));
      EXPECT_EQ(AOTChainBuilder::BuildChainProcessor(&allocator, graph, forcedChain.passes[0]),
                nullptr);
      EXPECT_EQ(AOTChainBuilder::BuildFPForPass(&allocator, graph, forcedChain.passes[0]), nullptr);
    }
  }
}

TGFX_TEST(AOTEffectTest, LinearChainSlotBoundaryIsChainInstructionBudget) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  constexpr size_t MaxOperators = ChainResourceCapacity.instructions - 1;
  for (size_t opCount : {MaxOperators, MaxOperators + 1}) {
    BlockAllocator allocator;
    auto texture = MakeTextureProcessor(context, &allocator, PixelFormat::RGBA_8888);
    auto matrix = ColorMatrixFragmentProcessor::Make(&allocator, IdentityColorMatrix);
    ASSERT_NE(texture, nullptr);
    ASSERT_NE(matrix, nullptr);
    std::vector<const FragmentProcessor*> processors = {texture.get()};
    processors.insert(processors.end(), opCount, matrix.get());
    AOTEffectGraph graph;
    ASSERT_TRUE(AOTEffectDecomposer::Lower(processors, &graph));
    // The chain kernel carries 16 instructions with a recycled 16-entry register file, so a
    // texture plus 15 operators fuses into one pass; the 16th operator moves to a second pass.
    AOTEffectPlan plan;
    ASSERT_TRUE(AOTEffectDecomposer::Decompose(graph, &plan));
    if (opCount == MaxOperators) {
      ASSERT_EQ(plan.passes.size(), 1u);
      EXPECT_EQ(plan.passes[0].kernel, AOTKernelKind::PointwiseChain);
      EXPECT_TRUE(AOTPlanExecutor::CanExecute(graph, plan));
    } else {
      ExpectSegmentedChainPlan(graph, plan, 2);
      EXPECT_EQ(plan.passes[0].nodes.size(), opCount);
      EXPECT_EQ(plan.passes[1].nodes.size(), 1u);
    }
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
    EXPECT_EQ(first.passes[index].output, second.passes[index].output);
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
// serves it in one pass rather than the multi-pass planner, which would materialize
// intermediates.
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

TGFX_TEST(AOTEffectTest, PerlinNoiseLookupIdentityAndComputationAreSeparate) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto first = MakePerlinProcessor(context);
  auto second = MakePerlinProcessor(context);
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);
  AOTEffectGraph firstGraph;
  AOTEffectGraph secondGraph;
  ASSERT_TRUE(AOTEffectDecomposer::Lower({first.get()}, &firstGraph));
  ASSERT_TRUE(AOTEffectDecomposer::Lower({second.get()}, &secondGraph));
  const auto* firstParameters =
      std::get_if<AOTPerlinNoiseParameters>(&firstGraph.nodeAt(AOTNodeID(1))->parameters);
  const auto* secondParameters =
      std::get_if<AOTPerlinNoiseParameters>(&secondGraph.nodeAt(AOTNodeID(1))->parameters);
  ASSERT_NE(firstParameters, nullptr);
  ASSERT_NE(secondParameters, nullptr);
  ASSERT_NE(firstParameters->lookupData, nullptr);
  ASSERT_NE(secondParameters->lookupData, nullptr);
  EXPECT_NE(firstParameters->permutationsView, secondParameters->permutationsView);
  EXPECT_NE(firstParameters->noiseView, secondParameters->noiseView);
  EXPECT_TRUE(AOTPerlinNoiseLookupsEquivalent(*firstParameters, *secondParameters));
  EXPECT_TRUE(AOTPerlinNoiseComputationsEquivalent(*firstParameters, *secondParameters));

  auto changed = *secondParameters;
  changed.baseFrequencyX *= 2.0f;
  EXPECT_TRUE(AOTPerlinNoiseLookupsEquivalent(*firstParameters, changed));
  EXPECT_FALSE(AOTPerlinNoiseComputationsEquivalent(*firstParameters, changed));
  changed = *secondParameters;
  changed.uvMatrix = Matrix::MakeTrans(1.0f, 0.0f);
  EXPECT_TRUE(AOTPerlinNoiseLookupsEquivalent(*firstParameters, changed));
  EXPECT_FALSE(AOTPerlinNoiseComputationsEquivalent(*firstParameters, changed));
  changed = *secondParameters;
  changed.numOctaves++;
  EXPECT_FALSE(AOTPerlinNoiseComputationsEquivalent(*firstParameters, changed));
  changed = *secondParameters;
  changed.noiseType = 1 - changed.noiseType;
  EXPECT_FALSE(AOTPerlinNoiseComputationsEquivalent(*firstParameters, changed));
  changed = *secondParameters;
  changed.stitchTiles = !changed.stitchTiles;
  EXPECT_FALSE(AOTPerlinNoiseComputationsEquivalent(*firstParameters, changed));
  changed = *secondParameters;
  changed.stitchWidth++;
  EXPECT_FALSE(AOTPerlinNoiseComputationsEquivalent(*firstParameters, changed));
  changed = *secondParameters;
  changed.stitchHeight++;
  EXPECT_FALSE(AOTPerlinNoiseComputationsEquivalent(*firstParameters, changed));
  changed = *secondParameters;
  changed.baseFrequencyY = std::numeric_limits<float>::quiet_NaN();
  EXPECT_FALSE(AOTPerlinNoiseComputationsEquivalent(changed, changed));

  changed = *secondParameters;
  auto differentTable = std::make_shared<AOTPerlinNoiseLookupData>(*changed.lookupData);
  differentTable->noise[0] ^= 1;
  changed.lookupData = differentTable;
  EXPECT_FALSE(AOTPerlinNoiseLookupsEquivalent(*firstParameters, changed));
  differentTable = std::make_shared<AOTPerlinNoiseLookupData>(*secondParameters->lookupData);
  differentTable->permutations[0] ^= 1;
  changed.lookupData = differentTable;
  EXPECT_FALSE(AOTPerlinNoiseLookupsEquivalent(*firstParameters, changed));
  changed = *secondParameters;
  changed.lookupData.reset();
  EXPECT_FALSE(AOTPerlinNoiseLookupsEquivalent(*firstParameters, changed));
  changed = *firstParameters;
  changed.lookupData.reset();
  EXPECT_TRUE(AOTPerlinNoiseLookupsEquivalent(*firstParameters, changed));

  AOTNodeBuilder builder;
  AOTNodeID geometry;
  AOTNodeID firstNoise;
  AOTNodeID secondNoise;
  ASSERT_TRUE(builder.addGeometryColor(&geometry));
  ASSERT_TRUE(builder.addPerlinNoiseSource(geometry, *firstParameters, &firstNoise));
  ASSERT_TRUE(builder.addPerlinNoiseSource(geometry, *secondParameters, &secondNoise));
  EXPECT_EQ(firstNoise, secondNoise);
  AOTEffectGraph canonical;
  ASSERT_TRUE(builder.finish(secondNoise, &canonical));
  const auto& canonicalFirst =
      std::get<AOTPerlinNoiseParameters>(canonical.nodeAt(firstNoise)->parameters);
  const auto& canonicalSecond =
      std::get<AOTPerlinNoiseParameters>(canonical.nodeAt(secondNoise)->parameters);
  EXPECT_EQ(canonicalFirst.permutationsView, canonicalSecond.permutationsView);
  EXPECT_EQ(canonicalFirst.noiseView, canonicalSecond.noiseView);
  EXPECT_EQ(canonicalFirst.lookupData, canonicalSecond.lookupData);
  EXPECT_EQ(first->textureAt(0), firstParameters->permutationsView->getTexture());
  EXPECT_EQ(second->textureAt(0), secondParameters->permutationsView->getTexture());
  EXPECT_EQ(second->textureAt(1), secondParameters->noiseView->getTexture());
}

TGFX_TEST(AOTEffectTest, PerlinNoiseReconstructionPreservesLookupEvidence) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  BlockAllocator allocator;
  auto original = MakePerlinProcessor(context);
  ASSERT_NE(original, nullptr);
  AOTEffectGraph graph;
  ASSERT_TRUE(AOTEffectDecomposer::Lower({original.get()}, &graph));
  const auto* parameters =
      std::get_if<AOTPerlinNoiseParameters>(&graph.nodeAt(AOTNodeID(1))->parameters);
  ASSERT_NE(parameters, nullptr);
  ASSERT_NE(parameters->lookupData, nullptr);
  auto rebuilt = PerlinNoiseFragmentProcessor::MakeFromAOTParameters(&allocator, *parameters, {});
  ASSERT_NE(rebuilt, nullptr);
  AOTEffectGraph rebuiltGraph;
  ASSERT_TRUE(AOTEffectDecomposer::Lower({rebuilt.get()}, &rebuiltGraph));
  const auto& rebuiltParameters =
      std::get<AOTPerlinNoiseParameters>(rebuiltGraph.nodeAt(AOTNodeID(1))->parameters);
  EXPECT_EQ(rebuiltParameters.lookupData, parameters->lookupData);
  EXPECT_TRUE(AOTPerlinNoiseComputationsEquivalent(*parameters, rebuiltParameters));

  auto paintingData = std::make_unique<PerlinNoiseShader::PaintingData>(
      parameters->baseFrequencyX, parameters->baseFrequencyY, parameters->stitchWidth,
      parameters->stitchHeight);
  auto rawRebuild = PerlinNoiseFragmentProcessor::MakeFromViews(
      &allocator, static_cast<PerlinNoiseType>(parameters->noiseType), parameters->numOctaves,
      parameters->stitchTiles, std::move(paintingData), parameters->permutationsView,
      parameters->noiseView, &parameters->uvMatrix, {});
  ASSERT_NE(rawRebuild, nullptr);
  AOTEffectGraph rawGraph;
  ASSERT_TRUE(AOTEffectDecomposer::Lower({rawRebuild.get()}, &rawGraph));
  const auto& rawParameters =
      std::get<AOTPerlinNoiseParameters>(rawGraph.nodeAt(AOTNodeID(1))->parameters);
  EXPECT_EQ(rawParameters.lookupData, nullptr);
  EXPECT_TRUE(AOTPerlinNoiseLookupsEquivalent(*parameters, rawParameters));
}

TGFX_TEST(AOTEffectTest, PerlinDensityBranchesShareLookupResources) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  constexpr std::array<float, 20> contrastMatrix = {
      2.0f, 0.0f, 0.0f, 0.0f, -0.5f, 0.0f, 2.0f, 0.0f, 0.0f, -0.5f,
      0.0f, 0.0f, 2.0f, 0.0f, -0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
  };
  for (float seed : {0.0f, 6903.0f}) {
    for (float density : {0.25f, 1.0f}) {
      for (float shift : {0.0f, 13.5f}) {
        auto noise = Shader::MakeFractalNoise(0.25f, 0.25f, 3, seed);
        ASSERT_NE(noise, nullptr);
        auto mono = MakeDarkDensityFilter(noise, density);
        auto bright = MakeBrightDensityFilter(noise, density);
        ASSERT_NE(mono, nullptr);
        ASSERT_NE(bright, nullptr);
        auto duo = Shader::MakeBlend(
            BlendMode::SrcOver,
            mono->makeWithColorFilter(ColorFilter::Blend(Color::Red(), BlendMode::SrcIn)),
            bright->makeWithColorFilter(ColorFilter::Blend(Color::Blue(), BlendMode::SrcIn)));
        auto multi = Shader::MakeBlend(
            BlendMode::DstIn, noise->makeWithColorFilter(ColorFilter::Matrix(contrastMatrix)),
            mono);
        ASSERT_NE(duo, nullptr);
        ASSERT_NE(multi, nullptr);
        const std::array<std::shared_ptr<Shader>, 3> shaders = {mono, duo, multi};
        for (size_t family = 0; family < shaders.size(); ++family) {
          SCOPED_TRACE(::testing::Message() << "family=" << family << " density=" << density
                                            << " seed=" << seed << " shift=" << shift);
          auto shader = shaders[family]->makeWithMatrix(Matrix::MakeTrans(shift, -shift));
          FPArgs args(context, 0, Rect::MakeWH(64.0f, 64.0f));
          auto processor = FragmentProcessor::Make(shader, args);
          ASSERT_NE(processor, nullptr);
          AOTEffectGraph graph;
          ASSERT_TRUE(AOTEffectDecomposer::Lower({processor.get()}, &graph));
          const AOTPerlinNoiseParameters* firstNoise = nullptr;
          size_t noiseNodes = 0;
          for (size_t index = 0; index < graph.nodeCount(); ++index) {
            const auto* node = graph.nodeAt(AOTNodeID(static_cast<uint32_t>(index)));
            if (node->kind != AOTEffectKind::PerlinNoiseSource) {
              continue;
            }
            const auto& parameters = std::get<AOTPerlinNoiseParameters>(node->parameters);
            ASSERT_NE(parameters.lookupData, nullptr);
            noiseNodes++;
            if (firstNoise == nullptr) {
              firstNoise = &parameters;
            }
            EXPECT_EQ(firstNoise->permutationsView, parameters.permutationsView);
            EXPECT_EQ(firstNoise->noiseView, parameters.noiseView);
            EXPECT_TRUE(AOTPerlinNoiseComputationsEquivalent(*firstNoise, parameters));
          }
          EXPECT_EQ(noiseNodes, 1u);
          AOTEffectPlan plan;
          ASSERT_TRUE(AOTEffectDecomposer::Decompose(graph, &plan));
          ASSERT_EQ(plan.passes.size(), 1u);
          EXPECT_EQ(plan.passes[0].kernel, AOTKernelKind::PointwiseGraph);
          ASSERT_TRUE(AOTPlanExecutor::CanExecute(graph, plan));
          auto wrongFamily = plan;
          wrongFamily.passes[0].kernel = AOTKernelKind::PointwiseChain;
          EXPECT_FALSE(AOTPlanExecutor::CanExecute(graph, wrongFamily));
          EXPECT_EQ(AOTChainBuilder::BuildChainProcessor(context->drawingAllocator(), graph,
                                                         wrongFamily.passes[0]),
                    nullptr);
          EXPECT_EQ(AOTChainBuilder::BuildFPForPass(context->drawingAllocator(), graph,
                                                    wrongFamily.passes[0]),
                    nullptr);
          auto rebuilt = AOTChainBuilder::BuildChainProcessor(context->drawingAllocator(), graph,
                                                              plan.passes[0]);
          ASSERT_NE(rebuilt, nullptr);
          const auto* chain = static_cast<const AOTPointwiseChainProcessor*>(rebuilt.get());
          EXPECT_TRUE(chain->hasNoise());
          EXPECT_EQ(chain->resourceProfile(), AOTResourceProfile::NoiseGraph);
          EXPECT_EQ(chain->leafCount(), 4u);
          ASSERT_EQ(chain->numCoordTransforms(), 1u);
          EXPECT_EQ(chain->coordTransform(0)->matrix, firstNoise->uvMatrix);
          ProgramInfo chainInfo(nullptr, nullptr, {rebuilt.get()}, 1, nullptr, BlendMode::SrcOver);
          FragmentProcessor::CoordTransformIter coordIter(&chainInfo);
          EXPECT_EQ(coordIter.next(), chain->coordTransform(0));
          for (size_t leaf = 0; leaf < chain->leafCount(); ++leaf) {
            EXPECT_EQ(coordIter.next(), chain->childProcessor(leaf)->coordTransform(0));
          }
        }
      }
    }
  }
}

TGFX_TEST(AOTEffectTest, PointwiseGraphPermutationsAreIndependentAndBounded) {
  PointwiseChainInputs inputs;
  inputs.gpKind = 1;
  inputs.hasCoverage = true;
  inputs.textureCount = 1;
  inputs.hasNoise = true;
  std::set<std::pair<uint32_t, uint32_t>> graphPairs;
  for (int xp = 0; xp < 3; ++xp) {
    inputs.xpType = xp;
    auto composed = ComposePointwiseGraph(inputs);
    ASSERT_TRUE(composed.has_value());
    EXPECT_EQ(composed->vertValues.size(), PointwiseChainShader::VD::COUNT);
    EXPECT_EQ(composed->fragValues.size(), PointwiseChainShader::FD::COUNT + 1);
    EXPECT_EQ(composed->fragValues[PointwiseGraphShader::FD::HAS_XP], xp);
    EXPECT_EQ(composed->fragValues[PointwiseGraphShader::FD::RESOURCE_KIND], 0);
    graphPairs.insert({PointwiseGraphShader::VD::domain().encode(composed->vertValues),
                       PointwiseGraphShader::FD::domain().encode(composed->fragValues)});
    EXPECT_FALSE(ComposePointwiseChain(inputs).has_value());
    inputs.hasNoise = false;
    EXPECT_TRUE(ComposePointwiseChain(inputs).has_value());
    EXPECT_FALSE(ComposePointwiseGraph(inputs).has_value());
    inputs.hasNoise = true;
    auto masked = inputs;
    masked.hasMask = true;
    masked.hasCoverageSubtree = true;
    auto maskedValues = ComposePointwiseGraph(masked);
    ASSERT_TRUE(maskedValues.has_value());
    EXPECT_EQ(maskedValues->vertValues, composed->vertValues);
    EXPECT_EQ(maskedValues->fragValues, composed->fragValues);
  }
  EXPECT_EQ(graphPairs.size(), 3u);
  for (int gpKind = 0; gpKind <= 4; ++gpKind) {
    for (int flags = 0; flags < 64; ++flags) {
      for (int xp = 0; xp < 3; ++xp) {
        PointwiseChainInputs base;
        base.gpKind = gpKind;
        base.hasCoverage = (flags & 1) != 0;
        base.hasUVCoord = (flags & 2) != 0;
        base.hasColor = (flags & 4) != 0;
        base.textureCount = (flags & 8) != 0 ? 1 : 0;
        base.hasMask = (flags & 16) != 0;
        base.hasCoverageSubtree = (flags & 32) != 0;
        base.xpType = xp;
        auto chainValues = ComposePointwiseChain(base);
        auto tiled = base;
        tiled.resourceProfile = AOTResourceProfile::TiledGraph;
        auto composed = ComposePointwiseGraph(tiled);
        ASSERT_EQ(composed.has_value(), chainValues.has_value());
        EXPECT_FALSE(ComposePointwiseChain(tiled).has_value());
        auto automatic = base;
        automatic.hasGradient = true;
        auto automaticValues = ComposePointwiseGraph(automatic);
        ASSERT_EQ(automaticValues.has_value(), composed.has_value());
        EXPECT_FALSE(ComposePointwiseChain(automatic).has_value());
        auto explicitChain = automatic;
        explicitChain.resourceProfile = AOTResourceProfile::Chain;
        EXPECT_FALSE(ComposePointwiseChain(explicitChain).has_value());
        EXPECT_FALSE(ComposePointwiseGraph(explicitChain).has_value());
        if (!composed) {
          continue;
        }
        auto expectedFragment = chainValues->fragValues;
        expectedFragment.push_back(1);
        EXPECT_EQ(composed->vertValues, chainValues->vertValues);
        EXPECT_EQ(composed->fragValues, expectedFragment);
        EXPECT_EQ(automaticValues->vertValues, composed->vertValues);
        EXPECT_EQ(automaticValues->fragValues, composed->fragValues);
        auto vertexIndex = PointwiseGraphShader::VD::domain().encode(composed->vertValues);
        auto fragmentIndex = PointwiseGraphShader::FD::domain().encode(composed->fragValues);
        EXPECT_TRUE(
            IsBuildablePermutation(PointwiseGraphShader().info(), vertexIndex, fragmentIndex));
        graphPairs.insert({vertexIndex, fragmentIndex});
        tiled.hasNoise = true;
        EXPECT_FALSE(ComposePointwiseGraph(tiled).has_value());
      }
    }
  }
  EXPECT_EQ(graphPairs.size(), EnumeratePointwiseChainReachable().size() + 3);
  EXPECT_EQ(EnumeratePointwiseGraphReachable(), graphPairs);
  EXPECT_EQ(EnumerateReachablePermutations("PointwiseGraphShader"), graphPairs);
  auto unsupported = inputs;
  unsupported.gpKind = 0;
  EXPECT_FALSE(ComposePointwiseGraph(unsupported).has_value());
  unsupported = inputs;
  unsupported.hasUVCoord = true;
  EXPECT_FALSE(ComposePointwiseGraph(unsupported).has_value());
  unsupported = inputs;
  unsupported.hasColor = true;
  EXPECT_FALSE(ComposePointwiseGraph(unsupported).has_value());
  unsupported = inputs;
  unsupported.textureCount = 0;
  EXPECT_FALSE(ComposePointwiseGraph(unsupported).has_value());
  unsupported = inputs;
  unsupported.hasCoverage = false;
  EXPECT_FALSE(ComposePointwiseGraph(unsupported).has_value());
  for (int xp : {-1, 3}) {
    unsupported = inputs;
    unsupported.xpType = xp;
    EXPECT_FALSE(ComposePointwiseGraph(unsupported).has_value());
  }
}

TGFX_TEST(AOTEffectTest, PerlinDAGRejectsDistinctNoiseAndPrunesUnusedInputs) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto original = MakePerlinProcessor(context);
  ASSERT_NE(original, nullptr);
  AOTEffectGraph originalGraph;
  ASSERT_TRUE(AOTEffectDecomposer::Lower({original.get()}, &originalGraph));
  auto noise = std::get<AOTPerlinNoiseParameters>(originalGraph.nodeAt(AOTNodeID(1))->parameters);
  ASSERT_TRUE(AOTPerlinNoiseParametersSupported(noise));
  for (int changedField = 0; changedField < 4; ++changedField) {
    AOTNodeBuilder builder;
    AOTNodeID geometry;
    AOTNodeID first;
    AOTNodeID second;
    AOTNodeID root;
    ASSERT_TRUE(builder.addGeometryColor(&geometry));
    ASSERT_TRUE(builder.addPerlinNoiseSource(geometry, noise, &first));
    auto changed = noise;
    if (changedField == 0) {
      changed.baseFrequencyX *= 2;
    } else if (changedField == 1) {
      changed.uvMatrix = Matrix::MakeTrans(1, 0);
    } else if (changedField == 2) {
      changed.numOctaves++;
    } else {
      changed.noiseType = 1 - changed.noiseType;
    }
    ASSERT_TRUE(builder.addPerlinNoiseSource(geometry, changed, &second));
    EXPECT_NE(first, second);
    AOTBlendParameters blend;
    blend.blendMode = static_cast<int>(BlendMode::SrcOver);
    blend.childType = 2;
    ASSERT_TRUE(builder.addBlend(first, second, blend, &root));
    AOTEffectGraph graph;
    ASSERT_TRUE(builder.finish(root, &graph));
    AOTEffectPlan plan;
    EXPECT_FALSE(AOTEffectDecomposer::Decompose(graph, &plan));
  }
  AOTNodeBuilder builder;
  AOTNodeID geometry;
  AOTNodeID first;
  ASSERT_TRUE(builder.addGeometryColor(&geometry));
  ASSERT_TRUE(builder.addPerlinNoiseSource(geometry, noise, &first));
  AOTNodeID current = first;
  AOTColorMatrixParameters matrix;
  matrix.matrix = IdentityColorMatrix;
  for (int index = 0; index < 33; ++index) {
    AOTNodeID next;
    ASSERT_TRUE(builder.addColorMatrix(current, matrix, &next));
    current = next;
  }
  AOTNodeID reused;
  ASSERT_TRUE(builder.addPerlinNoiseSource(current, noise, &reused));
  EXPECT_EQ(first, reused);
  AOTEffectGraph graph;
  ASSERT_TRUE(builder.finish(reused, &graph));
  AOTEffectPlan plan;
  ASSERT_TRUE(AOTEffectDecomposer::Decompose(graph, &plan));
  ASSERT_EQ(plan.passes.size(), 1u);
  EXPECT_EQ(plan.passes[0].nodes.size(), 1u);
  EXPECT_EQ(plan.passes[0].output, first);
  EXPECT_TRUE(AOTPlanExecutor::CanExecute(graph, plan));
  auto rebuilt =
      AOTChainBuilder::BuildChainProcessor(context->drawingAllocator(), graph, plan.passes[0]);
  ASSERT_NE(rebuilt, nullptr);
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

static void ExpectComposedClipsWithLocalMask(size_t rectCount, size_t rrectCount,
                                             bool rectAntiAlias, bool accepted) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  SCOPED_TRACE(rectCount);
  SCOPED_TRACE(rrectCount);
  SCOPED_TRACE(rectAntiAlias);
  BlockAllocator allocator;
  auto color = ConstColorProcessor::Make(&allocator, PMColor::White(), InputMode::ModulateRGBA);
  ASSERT_NE(color, nullptr);
  AOTEffectGraph graph;
  ASSERT_TRUE(AOTEffectDecomposer::Lower({color.get()}, &graph));
  AOTEffectPlan plan;
  ASSERT_TRUE(AOTEffectDecomposer::Decompose(graph, &plan));
  ASSERT_EQ(plan.passes.size(), 1u);
  PlacementPtr<FragmentProcessor> clips;
  for (size_t index = 0; index < rectCount; ++index) {
    auto rect =
        RectEffect::Make(&allocator, Rect::MakeLTRB(8, 8, 72, 72), Matrix::I(), rectAntiAlias);
    ASSERT_NE(rect, nullptr);
    clips = FragmentProcessor::Compose(&allocator, std::move(clips), std::move(rect));
  }
  for (size_t index = 0; index < rrectCount; ++index) {
    auto rrect =
        RRectEffect::Make(&allocator, RRect::MakeRectXY(Rect::MakeLTRB(16, 16, 80, 80), 12, 12));
    ASSERT_NE(rrect, nullptr);
    clips = FragmentProcessor::Compose(&allocator, std::move(clips), std::move(rrect));
  }
  ASSERT_NE(clips, nullptr);
  auto mask = MakeTextureProcessor(context, &allocator, PixelFormat::ALPHA_8);
  ASSERT_NE(mask, nullptr);
  auto processor = AOTChainBuilder::BuildChainProcessor(&allocator, graph, plan.passes[0],
                                                        {clips.get(), mask.get()});
  if (!accepted) {
    EXPECT_EQ(processor, nullptr);
    return;
  }
  ASSERT_NE(processor, nullptr);
  auto* chain = static_cast<const AOTPointwiseChainProcessor*>(processor.get());
  ASSERT_EQ(chain->slotCount(), 2 + rectCount + rrectCount);
  const auto& maskSlot = chain->slot(0);
  EXPECT_EQ(maskSlot.op, AOTChainOp::Texture);
  EXPECT_EQ(maskSlot.textureAlphaOnly, 1);
  EXPECT_EQ(maskSlot.textureModulateUnit, 1);
  EXPECT_EQ(chain->coverageRoot(), maskSlot.outRegister);
  EXPECT_EQ(chain->root(), static_cast<size_t>(chain->slot(1).outRegister));
  int previous = -4;
  for (size_t index = 2; index < chain->slotCount(); ++index) {
    const auto& slot = chain->slot(index);
    EXPECT_EQ(slot.op,
              index < 2 + rectCount ? AOTChainOp::AARectCoverage : AOTChainOp::RRectCoverage);
    EXPECT_EQ(slot.in0, previous);
    EXPECT_GE(slot.outRegister, 0);
    previous = slot.outRegister;
  }
  EXPECT_EQ(chain->clipCoverage(), previous);
}

TGFX_TEST(AOTEffectTest, ComposedAnalyticClipsWithLocalMaskRespectBudgets) {
  ExpectComposedClipsWithLocalMask(1, 1, true, true);
  ExpectComposedClipsWithLocalMask(1, 4, true, true);
  ExpectComposedClipsWithLocalMask(2, 1, true, false);
  ExpectComposedClipsWithLocalMask(1, 5, true, false);
  ExpectComposedClipsWithLocalMask(1, 1, false, false);
}

static PlacementPtr<FragmentProcessor> MakeAdditionalAnalyticClip(BlockAllocator* allocator,
                                                                  AOTChainOp op) {
  auto rect = Rect::MakeLTRB(30, 20, 90, 90);
  switch (op) {
    case AOTChainOp::AARectCoverage:
      return RectEffect::Make(allocator, rect);
    case AOTChainOp::LocalRectCoverage:
      return RectEffect::Make(allocator, rect, Matrix::MakeTrans(4, 4));
    case AOTChainOp::RRectCoverage:
      return RRectEffect::Make(allocator, RRect::MakeRectXY(rect, 8, 8));
    default:
      return nullptr;
  }
}

static void ExpectAdditionalClipPreservesCoverageSuffix(AOTChainOp op, bool withMask) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  for (size_t suffixCount = 0; suffixCount <= 2; ++suffixCount) {
    SCOPED_TRACE(suffixCount);
    SCOPED_TRACE(withMask);
    BlockAllocator allocator;
    auto texture = MakeTextureProcessor(context, &allocator, PixelFormat::RGBA_8888);
    ASSERT_NE(texture, nullptr);
    std::vector<PlacementPtr<RRectEffect>> suffixes;
    std::vector<const FragmentProcessor*> colorProcessors = {texture.get()};
    for (size_t index = 0; index < suffixCount; ++index) {
      auto rect = Rect::MakeLTRB(10 + static_cast<float>(index) * 4, 10, 70, 70);
      auto suffix = RRectEffect::Make(&allocator, RRect::MakeRectXY(rect, 8, 8));
      ASSERT_NE(suffix, nullptr);
      colorProcessors.push_back(suffix.get());
      suffixes.push_back(std::move(suffix));
    }
    AOTEffectGraph graph;
    ASSERT_TRUE(AOTEffectDecomposer::Lower(colorProcessors, &graph));
    AOTEffectPlan plan;
    ASSERT_TRUE(AOTEffectDecomposer::Decompose(graph, &plan));
    ASSERT_EQ(plan.passes.size(), 1u);
    ASSERT_TRUE(AOTPlanExecutor::CanExecute(graph, plan));
    auto original = AOTChainBuilder::BuildChainProcessor(&allocator, graph, plan.passes[0]);
    ASSERT_NE(original, nullptr);
    auto originalChain = static_cast<const AOTPointwiseChainProcessor*>(original.get());
    ASSERT_EQ(originalChain->slotCount(), suffixCount + 1);
    EXPECT_EQ(originalChain->root(), static_cast<size_t>(originalChain->slot(0).outRegister));
    EXPECT_EQ(originalChain->coverageRoot(), -1);
    EXPECT_EQ(originalChain->clipCoverage(),
              suffixCount == 0 ? -1 : originalChain->slot(suffixCount).outRegister);

    auto additionalClip = MakeAdditionalAnalyticClip(&allocator, op);
    ASSERT_NE(additionalClip, nullptr);
    std::vector<const FragmentProcessor*> coverageProcessors = {additionalClip.get()};
    PlacementPtr<FragmentProcessor> mask;
    if (withMask) {
      auto proxy = context->proxyProvider()->createTextureProxy({}, 2, 2, PixelFormat::ALPHA_8);
      ASSERT_NE(proxy, nullptr);
      mask = DeviceSpaceTextureEffect::Make(&allocator, std::move(proxy), Matrix::I());
      ASSERT_NE(mask, nullptr);
      coverageProcessors.push_back(mask.get());
    }
    auto rebuilt =
        AOTChainBuilder::BuildChainProcessor(&allocator, graph, plan.passes[0], coverageProcessors);
    ASSERT_NE(rebuilt, nullptr);
    auto chain = static_cast<const AOTPointwiseChainProcessor*>(rebuilt.get());
    ASSERT_EQ(chain->slotCount(), suffixCount + 2);
    EXPECT_EQ(chain->root(), static_cast<size_t>(chain->slot(0).outRegister));
    EXPECT_EQ(chain->coverageRoot(), -1);
    EXPECT_EQ(chain->hasMask(), withMask);
    int previousCoverage = -4;
    for (size_t index = 1; index <= suffixCount; ++index) {
      const auto& suffix = chain->slot(index);
      EXPECT_EQ(suffix.op, AOTChainOp::RRectCoverage);
      EXPECT_EQ(suffix.in0, previousCoverage);
      EXPECT_GE(suffix.outRegister, 0);
      previousCoverage = suffix.outRegister;
    }
    const auto& appended = chain->slot(suffixCount + 1);
    EXPECT_EQ(appended.op, op);
    EXPECT_EQ(appended.in0, previousCoverage);
    EXPECT_GE(appended.outRegister, 0);
    EXPECT_EQ(chain->clipCoverage(), appended.outRegister);
  }
}

TGFX_TEST(AOTEffectTest, DeviceRectPreservesCoverageSuffix) {
  ExpectAdditionalClipPreservesCoverageSuffix(AOTChainOp::AARectCoverage, false);
  ExpectAdditionalClipPreservesCoverageSuffix(AOTChainOp::AARectCoverage, true);
}

TGFX_TEST(AOTEffectTest, LocalRectPreservesCoverageSuffix) {
  ExpectAdditionalClipPreservesCoverageSuffix(AOTChainOp::LocalRectCoverage, true);
}

TGFX_TEST(AOTEffectTest, RRectPreservesCoverageSuffix) {
  ExpectAdditionalClipPreservesCoverageSuffix(AOTChainOp::RRectCoverage, true);
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

// a texture whose runtime input is a computed value — an alpha-only
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

// A two-child xfer whose input is a computed node — a mask texture
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

// : a coverage subtree whose non-root
// texture consumes the GP coverage unit. The runtime coverage-FP readback is tex * input.a and the
// subtree's input starts from the unit, so an A8 mask under an analytic root must modulate by the
// GP coverage. The builder only sets textureModulateUnit when the texture IS the coverage root,
// so a non-root texture drops the GP coverage: with GP coverage q=.5, mask m=.5 and an inner rect
// r=1, the runtime produces q*m*r=.25 while the chain produces m*r=.5.
TGFX_TEST(AOTEffectTest, NonRootCoverageTextureModulatesByUnit) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  BlockAllocator allocator;
  auto texture = MakeTextureProcessor(context, &allocator, PixelFormat::RGBA_8888);
  auto colorMatrix = ColorMatrixFragmentProcessor::Make(&allocator, IdentityColorMatrix);
  ASSERT_NE(texture, nullptr);
  ASSERT_NE(colorMatrix, nullptr);
  // A local A8 mask composed with a device-space AA rect: the Compose lowers through the general
  // coverage-subtree path, the texture consumes the unit, and the analytic rect is the root.
  auto mask = MakeTextureProcessor(context, &allocator, PixelFormat::ALPHA_8);
  ASSERT_NE(mask, nullptr);
  auto rect = RectEffect::Make(&allocator, Rect::MakeLTRB(10.5f, 10.5f, 90.5f, 90.5f));
  ASSERT_NE(rect, nullptr);
  auto coverage = FragmentProcessor::Compose(&allocator, std::move(mask), std::move(rect));
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
  // The A8 mask leaf's input is the coverage unit, so it must carry the unit modulation: the
  // runtime multiplies the mask by the unit's alpha exactly once, wherever in the subtree the
  // mask sits — a value that must not depend on which node consumes it.
  int maskSlot = -1;
  for (size_t index = 0; index < chain->slotCount(); ++index) {
    if (chain->slot(index).op == AOTChainOp::Texture && chain->slot(index).textureAlphaOnly == 1) {
      ASSERT_EQ(maskSlot, -1);
      maskSlot = static_cast<int>(index);
    }
  }
  ASSERT_GE(maskSlot, 0);
  EXPECT_EQ(chain->slot(static_cast<size_t>(maskSlot)).textureModulateUnit, 1);
}

// : the runtime applies Swizzle::ForRead to every
// texture lookup (GRAY_8 -> .rrra, RG_88 -> .rgrg), so a gray source reads (g, g, g, 1); the
// chain's plain leaf samples raw (g, 0, 0, 1) on GL/Metal R8 — the G and B channels silently
// drop to zero. GRAY_8/RG_88 textures are user-reachable as standalone sources through external
// hardware buffers (Android AHardwareBuffer), so TextureEffect::lowerToAOT now fails closed on
// these formats. Instantiating such a view needs a real GRAY_8 upload (a HardwareBuffer-backed
// image), which this test environment cannot construct — the format gate itself is the shipped
// behavior; a hardware-buffer-capable environment should extend this test with a pixel-level
// gray-image draw asserting the runtime route serves it.
TGFX_TEST(AOTEffectTest, ChainRejectsNonRGBAColorFormats) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  BlockAllocator allocator;
  auto colorMatrix = ColorMatrixFragmentProcessor::Make(&allocator, IdentityColorMatrix);
  ASSERT_NE(colorMatrix, nullptr);
  for (auto format : {PixelFormat::GRAY_8, PixelFormat::RG_88}) {
    auto grayLeaf = MakeTextureProcessor(context, &allocator, format);
    if (grayLeaf == nullptr ||
        static_cast<const TextureEffect*>(grayLeaf.get())->getTextureView() == nullptr) {
      // No view can be instantiated for this format here (no upload source exists): the gate's
      // observable behavior needs a HardwareBuffer-backed environment.
      continue;
    }
    SCOPED_TRACE(static_cast<int>(format));
    AOTEffectGraph graph = {};
    // The lowering must refuse the leaf (no swizzle contract on the chain's plain path).
    EXPECT_FALSE(AOTEffectDecomposer::Lower({grayLeaf.get(), colorMatrix.get()}, &graph));
  }
}

// Audit A6: the admission budget must count FINAL ENCODED instructions,
// not graph nodes. A computed-input texture occupies two slots (a raw sampling slot plus a
// TEX_MODULATE instruction after its producer), so a 16-node graph carrying one is 17 encoded
// instructions and must be refused by CanExecute (and by the DAG planner that consults it) —
// the pre-fix check compared the node count directly to the budget, then admitted an oversized
// instruction stream that BuildChainProcessor refused.
TGFX_TEST(AOTEffectTest, ComputedInputExpansionCountsTowardSlotBudget) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  BlockAllocator allocator;
  auto build = [&](size_t matrixCount) {
    std::vector<PlacementPtr<FragmentProcessor>> processors = {};
    auto mask = MakeTextureProcessor(context, &allocator, PixelFormat::ALPHA_8);
    auto image = MakeTextureProcessor(context, &allocator, PixelFormat::RGBA_8888);
    if (mask == nullptr || image == nullptr) {
      return false;
    }
    processors.push_back(std::move(mask));
    processors.push_back(std::move(image));
    for (size_t index = 0; index < matrixCount; ++index) {
      processors.push_back(ColorMatrixFragmentProcessor::Make(&allocator, IdentityColorMatrix));
    }
    std::vector<const FragmentProcessor*> raw = {};
    for (auto& fp : processors) {
      raw.push_back(fp.get());
    }
    AOTEffectGraph graph = {};
    AOTEffectPlan plan = {};
    // The DAG planner consults CanExecute: a budget overrun must surface as a planning refusal.
    return AOTEffectDecomposer::Lower(raw, &graph) && AOTEffectDecomposer::Decompose(graph, &plan);
  };
  // mask(1) + computed-input image(1) + 13 matrices = 15 nodes, 16 encoded: accepted.
  EXPECT_TRUE(build(ChainResourceCapacity.instructions - 3));
  // One more matrix = 16 nodes, 17 encoded: refused by the admission budget.
  EXPECT_FALSE(build(ChainResourceCapacity.instructions - 2));
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
  EXPECT_TRUE(AOTPlanExecutor::CanExecute(graph, plan));
  auto invalidProfile = plan;
  invalidProfile.passes[0].resourceProfile = static_cast<AOTResourceProfile>(99);
  EXPECT_FALSE(AOTPlanExecutor::CanExecute(graph, invalidProfile));
}

// Shader::makeWithColorFilter(ColorFilter::Blend(c, mode)) lowers to Perlin -> Xfermode-src(Const).
// The single-child xfer lowers its constant child from an explicit opaque-white designator, which
// the Perlin planner must skip rather than reject (regression: the designator was added to the
// lowering without the planner learning about it, sending these draws to the runtime path).
TGFX_TEST(AOTEffectTest, PerlinNoiseConstBlendFusesToSinglePass) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  BlockAllocator allocator;
  auto perlin = MakePerlinProcessor(context);
  ASSERT_NE(perlin, nullptr);
  auto white = ConstColorProcessor::Make(&allocator, PMColor::White(), InputMode::Ignore);
  ASSERT_NE(white, nullptr);
  auto blend = XfermodeFragmentProcessor::MakeFromSrcProcessor(&allocator, std::move(white),
                                                               BlendMode::SrcIn);
  ASSERT_NE(blend, nullptr);

  AOTEffectGraph graph;
  ASSERT_TRUE(AOTEffectDecomposer::Lower({perlin.get(), blend.get()}, &graph));
  // node0=geometry, node1=perlin, node2=white designator, node3=const (input = node2),
  // node4=blend(src=const, dst=perlin).
  ASSERT_EQ(graph.nodeCount(), 5u);
  EXPECT_EQ(graph.nodeAt(AOTNodeID(2))->kind, AOTEffectKind::GeometryWhiteInput);
  AOTEffectPlan plan;
  ASSERT_TRUE(AOTEffectDecomposer::Decompose(graph, &plan));
  ASSERT_EQ(plan.passes.size(), 1u);
  EXPECT_EQ(plan.passes[0].kernel, AOTKernelKind::PerlinNoiseFill);
  EXPECT_EQ(plan.passes[0].nodes, std::vector<AOTNodeID>({AOTNodeID(1), AOTNodeID(4)}));
  EXPECT_TRUE(AOTPlanExecutor::CanExecute(graph, plan));
  EXPECT_NE(AOTChainBuilder::BuildFPForPass(&allocator, graph, plan.passes[0]), nullptr);
}

// The SVG feTurbulence shape with a trailing colorize: Perlin -> luminanceToAlpha -> threshold ->
// Xfermode-src(Const). Seven graph nodes but only three slots; the designator must not count
// against the pass budget.
TGFX_TEST(AOTEffectTest, PerlinNoiseOpsThenConstBlendFusesToSinglePass) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  BlockAllocator allocator;
  auto perlin = MakePerlinProcessor(context);
  ASSERT_NE(perlin, nullptr);
  auto colorMatrix = ColorMatrixFragmentProcessor::Make(&allocator, IdentityColorMatrix);
  auto alphaThreshold = AlphaThresholdFragmentProcessor::Make(&allocator, 0.5f);
  auto white = ConstColorProcessor::Make(&allocator, PMColor::White(), InputMode::Ignore);
  ASSERT_NE(colorMatrix, nullptr);
  ASSERT_NE(alphaThreshold, nullptr);
  ASSERT_NE(white, nullptr);
  auto blend = XfermodeFragmentProcessor::MakeFromSrcProcessor(&allocator, std::move(white),
                                                               BlendMode::SrcIn);
  ASSERT_NE(blend, nullptr);

  AOTEffectGraph graph;
  ASSERT_TRUE(AOTEffectDecomposer::Lower(
      {perlin.get(), colorMatrix.get(), alphaThreshold.get(), blend.get()}, &graph));
  ASSERT_EQ(graph.nodeCount(), 7u);
  AOTEffectPlan plan;
  ASSERT_TRUE(AOTEffectDecomposer::Decompose(graph, &plan));
  ASSERT_EQ(plan.passes.size(), 1u);
  EXPECT_EQ(plan.passes[0].kernel, AOTKernelKind::PerlinNoiseFill);
  EXPECT_EQ(plan.passes[0].nodes,
            std::vector<AOTNodeID>({AOTNodeID(1), AOTNodeID(2), AOTNodeID(3), AOTNodeID(6)}));
  EXPECT_TRUE(AOTPlanExecutor::CanExecute(graph, plan));
  EXPECT_NE(AOTChainBuilder::BuildFPForPass(&allocator, graph, plan.passes[0]), nullptr);
}

// The Perlin blend slot uploads the constant operand as-is. An operand whose value depends on its
// input edge (a modulating const fed by the chain value) cannot ride that slot and must be refused
// by the planner, the executor check, and the builder alike; an Ignore-mode operand fed by the
// chain value is input-independent and stays accepted.
TGFX_TEST(AOTEffectTest, PerlinNoiseBlendRefusesInputDependentConstOperand) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  BlockAllocator allocator;
  auto perlin = MakePerlinProcessor(context);
  ASSERT_NE(perlin, nullptr);
  auto buildGraph = [&](int inputMode, AOTEffectGraph* graph) {
    AOTNodeBuilder builder;
    AOTNodeID geometry;
    AOTNodeID noise;
    AOTNodeID constant;
    AOTNodeID blended;
    if (!builder.addGeometryColor(&geometry) || !perlin->lowerToAOT(&builder, geometry, &noise)) {
      return false;
    }
    AOTConstColorParameters constParameters = {};
    constParameters.color = {1.0f, 1.0f, 1.0f, 1.0f};
    constParameters.inputMode = inputMode;
    AOTBlendParameters blendParameters = {};
    blendParameters.blendMode = static_cast<int>(BlendMode::SrcIn);
    blendParameters.childType = 1;  // XfermodeFragmentProcessor::Child::SrcChild
    return builder.addConstColor(noise, constParameters, &constant) &&
           builder.addBlend(constant, noise, blendParameters, &blended) &&
           builder.finish(blended, graph);
  };
  auto servedByPerlin = [&](const AOTEffectGraph& graph) {
    AOTEffectPlan plan;
    return AOTEffectDecomposer::Decompose(graph, &plan) && plan.passes.size() == 1 &&
           plan.passes[0].kernel == AOTKernelKind::PerlinNoiseFill;
  };
  AOTEffectGraph ignoreGraph;
  ASSERT_TRUE(buildGraph(static_cast<int>(InputMode::Ignore), &ignoreGraph));
  EXPECT_TRUE(servedByPerlin(ignoreGraph));
  AOTEffectGraph modulateGraph;
  ASSERT_TRUE(buildGraph(static_cast<int>(InputMode::ModulateRGBA), &modulateGraph));
  EXPECT_FALSE(servedByPerlin(modulateGraph));
  AOTEffectPlan forcedPlan;
  forcedPlan.passes.emplace_back();
  forcedPlan.passes[0].kernel = AOTKernelKind::PerlinNoiseFill;
  forcedPlan.passes[0].nodes = {AOTNodeID(1), AOTNodeID(3)};
  forcedPlan.passes[0].output = AOTNodeID(3);
  forcedPlan.output = modulateGraph.root();
  EXPECT_FALSE(AOTPlanExecutor::CanExecute(modulateGraph, forcedPlan));
  EXPECT_EQ(AOTChainBuilder::BuildFPForPass(&allocator, modulateGraph, forcedPlan.passes[0]),
            nullptr);
}

// Table-driven admission matrix. Each row is one processor chain plus the route it must take; the
// invariants below are checked for every row, so a new effect or variant is one added row:
//   - admission implies construction: every plan Decompose returns builds its processor;
//   - the analysis verdict is the production verdict (FusablePointwise iff Decompose plans it);
//   - Decompose only returns plans CanExecute accepts, with the expected number of passes.
struct AdmissionCase {
  const char* name = "";
  // Builds the color processors into owners; returns false when the fixture cannot be created.
  std::function<bool(Context*, BlockAllocator*, std::vector<PlacementPtr<FragmentProcessor>>*)>
      make;
  bool lowers = true;
  bool planned = true;
  AOTKernelKind kernel = AOTKernelKind::PointwiseChain;
  size_t passes = 1;
};

static PlacementPtr<FragmentProcessor> MakeRadialGradient(BlockAllocator* allocator) {
  auto colorizer = SingleIntervalGradientColorizer::Make(allocator, Color::Red(), Color::Blue());
  auto layout = RadialGradientLayout::Make(allocator, Matrix::MakeTrans(4, 6));
  return ClampedGradientEffect::Make(allocator, std::move(colorizer), std::move(layout),
                                     Color::Red(), Color::Blue());
}

TGFX_TEST(AOTEffectTest, AdmissionMatrixKeepsPlanningAndConstructionAligned) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  using Owners = std::vector<PlacementPtr<FragmentProcessor>>;
  auto texture = [](Context* c, BlockAllocator* a) {
    return MakeTextureProcessor(c, a, PixelFormat::RGBA_8888);
  };
  auto matrix = [](BlockAllocator* a) {
    return ColorMatrixFragmentProcessor::Make(a, IdentityColorMatrix);
  };
  auto deviceTexture = [](Context* c, BlockAllocator* a) -> PlacementPtr<FragmentProcessor> {
    auto proxy = c->proxyProvider()->createTextureProxy({}, 2, 2, PixelFormat::RGBA_8888);
    return proxy != nullptr ? DeviceSpaceTextureEffect::Make(a, std::move(proxy), Matrix::I())
                            : nullptr;
  };
  const AdmissionCase cases[] = {
      {"texture+matrix",
       [&](Context* c, BlockAllocator* a, Owners* o) {
         o->push_back(texture(c, a));
         o->push_back(matrix(a));
         return true;
       }},
      {"tiled-repeat+luma",
       [&](Context* c, BlockAllocator* a, Owners* o) {
         o->push_back(MakeTiledTextureProcessor(c, a, TileMode::Repeat, TileMode::Mirror,
                                                PixelFormat::RGBA_8888, SrcRectConstraint::Fast,
                                                std::nullopt));
         o->push_back(LumaFragmentProcessor::Make(a));
         return true;
       }},
      {"two-texture-blend",
       [&](Context* c, BlockAllocator* a, Owners* o) {
         o->push_back(XfermodeFragmentProcessor::MakeFromTwoProcessors(
             a, texture(c, a), texture(c, a), BlendMode::Multiply));
         return true;
       }},
      {"radial-gradient+matrix",
       [&](Context*, BlockAllocator* a, Owners* o) {
         o->push_back(MakeRadialGradient(a));
         o->push_back(matrix(a));
         return true;
       },
       true, true, AOTKernelKind::PointwiseGraph},
      {"device-texture+matrix",
       [&](Context* c, BlockAllocator* a, Owners* o) {
         o->push_back(deviceTexture(c, a));
         o->push_back(matrix(a));
         return true;
       },
       true, true, AOTKernelKind::PointwiseChain},
      {"device-texture+three-ops",
       [&](Context* c, BlockAllocator* a, Owners* o) {
         o->push_back(deviceTexture(c, a));
         o->push_back(matrix(a));
         o->push_back(LumaFragmentProcessor::Make(a));
         o->push_back(matrix(a));
         return true;
       },
       true, true, AOTKernelKind::PointwiseChain},
      {"unsupported-lowering",
       [&](Context*, BlockAllocator* a, Owners* o) {
         o->push_back(a->make<ShapeRejectingFragmentProcessor>());
         return true;
       },
       false, false},
  };
  for (const auto& row : cases) {
    SCOPED_TRACE(row.name);
    BlockAllocator allocator;
    Owners owners;
    ASSERT_TRUE(row.make(context, &allocator, &owners));
    std::vector<const FragmentProcessor*> processors;
    for (const auto& owner : owners) {
      ASSERT_NE(owner, nullptr);
      processors.push_back(owner.get());
    }
    AOTEffectGraph graph;
    bool lowered = AOTEffectDecomposer::Lower(processors, &graph);
    EXPECT_EQ(lowered, row.lowers);
    auto analysis = AOTEffectDecomposer::AnalyzeChain(processors);
    if (!lowered) {
      EXPECT_NE(analysis.outcome, AOTDecomposeOutcome::FusablePointwise);
      continue;
    }
    AOTEffectPlan plan;
    bool planned = AOTEffectDecomposer::Decompose(graph, &plan);
    EXPECT_EQ(planned, row.planned);
    EXPECT_EQ(analysis.outcome == AOTDecomposeOutcome::FusablePointwise, planned);
    if (!planned) {
      continue;
    }
    ASSERT_EQ(plan.passes.size(), row.passes);
    EXPECT_EQ(plan.passes[0].kernel, row.kernel);
    EXPECT_TRUE(AOTPlanExecutor::CanExecute(graph, plan));
    EXPECT_NE(AOTChainBuilder::BuildFPForPass(&allocator, graph, plan.passes[0]), nullptr);
  }
}

}  // namespace tgfx

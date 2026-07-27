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
#include "gpu/PrecompiledProgramCreator.h"
#include "gpu/PrecompiledShaderCache.h"
#include "gpu/ProgramInfo.h"
#include "gpu/ProxyProvider.h"
#include "gpu/processors/AARectEffect.h"
#include "gpu/processors/ColorMatrixFragmentProcessor.h"
#include "gpu/processors/DefaultGeometryProcessor.h"
#include "gpu/processors/LumaFragmentProcessor.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "gtest/gtest.h"
#include "tgfx/core/ColorSpace.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/gpu/PixelFormat.h"
#include "utils/ProjectPath.h"
#include "utils/TestUtils.h"

namespace tgfx {

static constexpr std::array<float, 20> IdentityColorMatrix = {
    1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0,
};

static PlacementPtr<FragmentProcessor> MakeTextureProcessor(Context* context,
                                                            BlockAllocator* allocator,
                                                            PixelFormat format) {
  auto proxy = context->proxyProvider()->createTextureProxy({}, 2, 2, format);
  if (proxy == nullptr || proxy->getTextureView() == nullptr) {
    return nullptr;
  }
  return TextureEffect::Make(allocator, std::move(proxy));
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

TGFX_TEST(AOTEffectTest, UnsupportedProcessorFailsAtomically) {
  BlockAllocator allocator;
  auto unsupported = AARectEffect::Make(&allocator, Rect::MakeWH(2, 2));
  ASSERT_NE(unsupported, nullptr);
  AOTEffectGraph graph;
  EXPECT_FALSE(AOTEffectDecomposer::Lower({unsupported.get()}, &graph));
  EXPECT_EQ(graph.nodeCount(), 0u);
  EXPECT_FALSE(graph.root().isValid());
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
  ASSERT_TRUE(AOTEffectDecomposer::Decompose(graph, AOTDecompositionMode::PreferFusion, &plan));
  ASSERT_EQ(plan.passes.size(), 1u);
  EXPECT_EQ(plan.passes[0].kernel, AOTKernelKind::TextureColorMatrix);
  EXPECT_EQ(plan.passes[0].nodes, std::vector<AOTNodeID>({AOTNodeID(1), AOTNodeID(2)}));
  EXPECT_FALSE(plan.passes[0].materializesOutput);
}

TGFX_TEST(AOTEffectTest, TripleChainSupportsFusedAndStandardPlans) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  BlockAllocator allocator;
  AOTEffectGraph graph;
  ASSERT_TRUE(BuildTripleGraph(context, &allocator, &graph));

  AOTEffectPlan fusedPlan;
  ASSERT_TRUE(
      AOTEffectDecomposer::Decompose(graph, AOTDecompositionMode::PreferFusion, &fusedPlan));
  ASSERT_EQ(fusedPlan.passes.size(), 2u);
  EXPECT_EQ(fusedPlan.passes[0].kernel, AOTKernelKind::TextureColorMatrix);
  EXPECT_EQ(fusedPlan.passes[1].kernel, AOTKernelKind::TexturedLuma);
  EXPECT_TRUE(fusedPlan.passes[0].materializesOutput);
  EXPECT_FALSE(fusedPlan.passes[1].materializesOutput);
  EXPECT_EQ(fusedPlan.passes[1].dependencies, std::vector<uint32_t>({0}));

  AOTEffectPlan standardPlan;
  ASSERT_TRUE(AOTEffectDecomposer::Decompose(graph, AOTDecompositionMode::Standard, &standardPlan));
  ASSERT_EQ(standardPlan.passes.size(), 3u);
  EXPECT_EQ(standardPlan.passes[0].kernel, AOTKernelKind::TextureFill);
  EXPECT_EQ(standardPlan.passes[1].kernel, AOTKernelKind::TexturedColorMatrix);
  EXPECT_EQ(standardPlan.passes[2].kernel, AOTKernelKind::TexturedLuma);
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
  ASSERT_TRUE(AOTEffectDecomposer::Decompose(graph, AOTDecompositionMode::PreferFusion, &first));
  ASSERT_TRUE(AOTEffectDecomposer::Decompose(graph, AOTDecompositionMode::PreferFusion, &second));
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

static std::string DecomposeBundlePath() {
  std::string backend = TGFX_BACKEND_NAME;
  auto pos = backend.find('-');
  if (pos != std::string::npos) {
    backend = backend.substr(0, pos);
  }
  return "resources/shaders/shader_bundle." + backend + ".bin";
}

// L2 activation milestone: with decomposition enabled, CreateDecomposedProgram must turn a
// texture+ColorMatrix draw into a real precompiled Program (not nullptr) by fusing it into a single
// TextureColorMatrix pass and mapping to the existing TextureColorMatrixShader. This exercises the
// full L2 route end to end: lowerToAOT -> Lower -> Decompose -> plan->program mapping ->
// BuildProgramFromMatch. The produced program must originate from a precompiled artifact.
TGFX_TEST(AOTEffectTest, DecomposedTextureColorMatrixProducesProgram) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto* cache = context->precompiledShaderCache();
  ASSERT_TRUE(cache->loadBundle(ProjectPath::Absolute(DecomposeBundlePath())));
  cache->setDecompositionEnabled(true);

  auto proxy = RenderTargetProxy::Make(context, 2, 2, /*alphaOnly=*/false);
  ASSERT_NE(proxy, nullptr);
  auto renderTarget = proxy->getRenderTarget();
  ASSERT_NE(renderTarget, nullptr);
  auto* allocator = context->drawingAllocator();
  auto geometryProcessor =
      DefaultGeometryProcessor::Make(allocator, {}, 2, 2, AAType::None, {}, {});
  auto texture = MakeTextureProcessor(context, allocator, PixelFormat::RGBA_8888);
  auto colorMatrix = ColorMatrixFragmentProcessor::Make(allocator, IdentityColorMatrix);
  ASSERT_NE(geometryProcessor, nullptr);
  ASSERT_NE(texture, nullptr);
  ASSERT_NE(colorMatrix, nullptr);

  std::vector<FragmentProcessor*> fragmentProcessors = {texture.get(), colorMatrix.get()};
  ProgramInfo info(renderTarget.get(), geometryProcessor.get(), std::move(fragmentProcessors),
                   /*numColorProcessors=*/2, /*xferProcessor=*/nullptr, BlendMode::SrcOver);

  auto program = PrecompiledProgramCreator::CreateDecomposedProgram(context, &info);
  ASSERT_NE(program, nullptr);
  EXPECT_EQ(program->getProvenance().program, ProgramOrigin::PrecompiledArtifact);

  cache->setDecompositionEnabled(false);
  cache->unload();
}

// L2 single-pass mapping: with decomposition enabled, a lone plain-texture color chain must decompose
// into a single TextureFill pass and map to the existing TextureFillShader, producing a program from
// a precompiled artifact (no new variant). This mirrors TryMatchTextureFill's encoding via the L2
// route rather than the L3 handwritten matcher.
TGFX_TEST(AOTEffectTest, DecomposedTextureFillProducesProgram) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto* cache = context->precompiledShaderCache();
  ASSERT_TRUE(cache->loadBundle(ProjectPath::Absolute(DecomposeBundlePath())));
  cache->setDecompositionEnabled(true);

  auto proxy = RenderTargetProxy::Make(context, 2, 2, /*alphaOnly=*/false);
  ASSERT_NE(proxy, nullptr);
  auto renderTarget = proxy->getRenderTarget();
  ASSERT_NE(renderTarget, nullptr);
  auto* allocator = context->drawingAllocator();
  auto geometryProcessor =
      DefaultGeometryProcessor::Make(allocator, {}, 2, 2, AAType::None, {}, {});
  auto texture = MakeTextureProcessor(context, allocator, PixelFormat::RGBA_8888);
  ASSERT_NE(geometryProcessor, nullptr);
  ASSERT_NE(texture, nullptr);

  std::vector<FragmentProcessor*> fragmentProcessors = {texture.get()};
  ProgramInfo info(renderTarget.get(), geometryProcessor.get(), std::move(fragmentProcessors),
                   /*numColorProcessors=*/1, /*xferProcessor=*/nullptr, BlendMode::SrcOver);

  auto program = PrecompiledProgramCreator::CreateDecomposedProgram(context, &info);
  ASSERT_NE(program, nullptr);
  EXPECT_EQ(program->getProvenance().program, ProgramOrigin::PrecompiledArtifact);

  cache->setDecompositionEnabled(false);
  cache->unload();
}

}  // namespace tgfx

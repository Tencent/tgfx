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

#include <cstring>
#include <string>
#include <vector>
#include "base/TGFXTest.h"
#include "gpu/EmbeddedShaderBundles.h"
#include "gpu/GlobalCache.h"
#include "gpu/PrecompiledShaderCache.h"
#include "gtest/gtest.h"
#include "tgfx/core/Bitmap.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/ColorFilter.h"
#include "tgfx/core/ColorSpace.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Shader.h"
#include "tgfx/core/Surface.h"
#include "tgfx/gpu/Context.h"
#include "utils/TestUtils.h"

namespace tgfx {

#ifndef TGFX_BACKEND_NAME
#define TGFX_BACKEND_NAME "opengl"
#endif

// Systematic AOT-vs-runtime equivalence check. Each scene is rendered twice: once with the
// precompiled bundle loaded (PrecompiledProgramCreator serves matched variants from the AOT
// artifacts) and once with no bundle (ProgramBuilder generates the shader at runtime). The two
// renders must be byte-identical: a precompiled variant that diverges from the runtime codegen
// (e.g. the tiled-fill Dimension-normalization class of bug) shows up as a mismatch here, even for
// scenes that no screenshot baseline happens to cover.

static std::string ConsistencyBundlePath() {
  std::string backend = TGFX_BACKEND_NAME;
  auto pos = backend.find('-');
  if (pos != std::string::npos) {
    backend = backend.substr(0, pos);
  }
  return "resources/shaders/shader_bundle." + backend + ".bin";
}

// Renders the given paint over a full-surface rect into outBitmap. When useBundle is true the
// precompiled bundle is loaded so matched draws take the AOT path; otherwise the cache is unloaded
// so every draw goes through ProgramBuilder.
static void RenderPaintOnce(const Paint& paint, int width, int height, bool useBundle,
                            Bitmap* outBitmap) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto* cache = context->precompiledShaderCache();
  if (useBundle) {
    ASSERT_TRUE(cache->loadBundle(ProjectPath::Absolute(ConsistencyBundlePath())));
  } else {
    cache->unload();
  }
  context->globalCache()->clearPrograms();
  auto surface = Surface::Make(context, width, height);
  ASSERT_TRUE(surface != nullptr);
  surface->getCanvas()->drawRect(
      Rect::MakeWH(static_cast<float>(width), static_cast<float>(height)), paint);
  context->flushAndSubmit(true);
  ASSERT_TRUE(outBitmap->allocPixels(width, height));
  auto* pixels = outBitmap->lockPixels();
  ASSERT_TRUE(pixels != nullptr);
  ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
  outBitmap->unlockPixels();
  if (useBundle) {
    cache->unload();
  }
}

// Renders the given image (optionally through an image filter) into outBitmap, with or without the
// precompiled bundle.
static void RenderImageOnce(const std::shared_ptr<Image>& image,
                            const std::shared_ptr<ImageFilter>& filter, int width, int height,
                            bool useBundle, Bitmap* outBitmap) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto* cache = context->precompiledShaderCache();
  if (useBundle) {
    ASSERT_TRUE(cache->loadBundle(ProjectPath::Absolute(ConsistencyBundlePath())));
  } else {
    cache->unload();
  }
  context->globalCache()->clearPrograms();
  auto surface = Surface::Make(context, width, height);
  ASSERT_TRUE(surface != nullptr);
  auto drawImage = filter != nullptr ? image->makeWithFilter(filter) : image;
  ASSERT_TRUE(drawImage != nullptr);
  surface->getCanvas()->drawImage(drawImage, 0, 0);
  context->flushAndSubmit(true);
  ASSERT_TRUE(outBitmap->allocPixels(width, height));
  auto* pixels = outBitmap->lockPixels();
  ASSERT_TRUE(pixels != nullptr);
  ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
  outBitmap->unlockPixels();
  if (useBundle) {
    cache->unload();
  }
}

static void ExpectBitmapsIdentical(const char* label, const Bitmap& aotBitmap,
                                   const Bitmap& runtimeBitmap, int width, int height) {
  auto* aotPixels = const_cast<Bitmap&>(aotBitmap).lockPixels();
  auto* runtimePixels = const_cast<Bitmap&>(runtimeBitmap).lockPixels();
  ASSERT_TRUE(aotPixels != nullptr && runtimePixels != nullptr);
  size_t totalBytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
  int cmp = std::memcmp(aotPixels, runtimePixels, totalBytes);
  int maxDiff = 0;
  size_t diffCount = 0;
  auto* a = static_cast<const uint8_t*>(aotPixels);
  auto* r = static_cast<const uint8_t*>(runtimePixels);
  for (size_t i = 0; i < totalBytes; i++) {
    int d = std::abs(static_cast<int>(a[i]) - static_cast<int>(r[i]));
    if (d > 0) {
      diffCount++;
    }
    if (d > maxDiff) {
      maxDiff = d;
    }
  }
  const_cast<Bitmap&>(aotBitmap).unlockPixels();
  const_cast<Bitmap&>(runtimeBitmap).unlockPixels();
  EXPECT_EQ(cmp, 0) << "AOT vs runtime render diverged for scene: " << label
                    << " (maxChannelDiff=" << maxDiff << ", diffBytes=" << diffCount << "/"
                    << totalBytes << ")";
}

static void ExpectShaderConsistent(const char* label, const std::shared_ptr<Shader>& shader,
                                   int width, int height) {
  ASSERT_TRUE(shader != nullptr);
  Paint paint = {};
  paint.setShader(shader);
  Bitmap aotBitmap = {};
  Bitmap runtimeBitmap = {};
  RenderPaintOnce(paint, width, height, true, &aotBitmap);
  RenderPaintOnce(paint, width, height, false, &runtimeBitmap);
  ExpectBitmapsIdentical(label, aotBitmap, runtimeBitmap, width, height);
}

// Renders the given paint into a Display-P3 surface, so any sRGB content is converted through a
// ColorSpaceXformEffect (the shader whose pipeline flags were folded into the CSFlags uniform).
static void RenderPaintToP3Once(const Paint& paint, int width, int height, bool useBundle,
                                Bitmap* outBitmap) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto* cache = context->precompiledShaderCache();
  if (useBundle) {
    ASSERT_TRUE(cache->loadBundle(ProjectPath::Absolute(ConsistencyBundlePath())));
  } else {
    cache->unload();
  }
  context->globalCache()->clearPrograms();
  auto surface = Surface::Make(context, width, height, false, 1, false, 0, ColorSpace::DisplayP3());
  ASSERT_TRUE(surface != nullptr);
  surface->getCanvas()->drawRect(
      Rect::MakeWH(static_cast<float>(width), static_cast<float>(height)), paint);
  context->flushAndSubmit(true);
  ASSERT_TRUE(outBitmap->allocPixels(width, height));
  auto* pixels = outBitmap->lockPixels();
  ASSERT_TRUE(pixels != nullptr);
  ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
  outBitmap->unlockPixels();
  if (useBundle) {
    cache->unload();
  }
}

static void ExpectShaderConsistentP3(const char* label, const std::shared_ptr<Shader>& shader,
                                     int width, int height) {
  ASSERT_TRUE(shader != nullptr);
  Paint paint = {};
  paint.setShader(shader);
  Bitmap aotBitmap = {};
  Bitmap runtimeBitmap = {};
  RenderPaintToP3Once(paint, width, height, true, &aotBitmap);
  RenderPaintToP3Once(paint, width, height, false, &runtimeBitmap);
  ExpectBitmapsIdentical(label, aotBitmap, runtimeBitmap, width, height);
}

static void ExpectImageFilterConsistent(const char* label, const std::shared_ptr<Image>& image,
                                        const std::shared_ptr<ImageFilter>& filter, int width,
                                        int height) {
  ASSERT_TRUE(image != nullptr);
  Bitmap aotBitmap = {};
  Bitmap runtimeBitmap = {};
  RenderImageOnce(image, filter, width, height, true, &aotBitmap);
  RenderImageOnce(image, filter, width, height, false, &runtimeBitmap);
  ExpectBitmapsIdentical(label, aotBitmap, runtimeBitmap, width, height);
}

// Tiled texture fills: an image shader drawn over a rect produces a TiledTextureEffect for the
// non-clamp tile modes. Covers the ShaderMode values the precompiled TiledTextureFillShader
// supports (repeat/mirror/clamp-to-border), which is where the Dimension-normalization bug lived.
TGFX_TEST(AOTRenderConsistencyTest, TiledTextureFillModes) {
  auto image = MakeImage("resources/apitest/test_timestretch.png");
  ASSERT_TRUE(image != nullptr);
  int width = 200;
  int height = 200;
  ExpectShaderConsistent("tiled-repeat",
                         Shader::MakeImageShader(image, TileMode::Repeat, TileMode::Repeat), width,
                         height);
  ExpectShaderConsistent("tiled-mirror",
                         Shader::MakeImageShader(image, TileMode::Mirror, TileMode::Mirror), width,
                         height);
  ExpectShaderConsistent("tiled-clamp",
                         Shader::MakeImageShader(image, TileMode::Clamp, TileMode::Clamp), width,
                         height);
  ExpectShaderConsistent("tiled-decal",
                         Shader::MakeImageShader(image, TileMode::Decal, TileMode::Decal), width,
                         height);
  ExpectShaderConsistent("tiled-repeat-mirror",
                         Shader::MakeImageShader(image, TileMode::Repeat, TileMode::Mirror), width,
                         height);
}

// Gaussian blur over a tiled source: exercises GaussianBlur1DShader with a TiledTextureEffect child
// (HAS_TILED_CHILD) for the non-clamp tile modes, plus the plain-texture child for clamp.
TGFX_TEST(AOTRenderConsistencyTest, GaussianBlurTileModes) {
  auto image = MakeImage("resources/apitest/test_timestretch.png");
  ASSERT_TRUE(image != nullptr);
  int width = image->width() + 40;
  int height = image->height() + 40;
  ExpectImageFilterConsistent("blur-clamp", image, ImageFilter::Blur(6, 6, TileMode::Clamp), width,
                              height);
  ExpectImageFilterConsistent("blur-repeat", image, ImageFilter::Blur(6, 6, TileMode::Repeat),
                              width, height);
  ExpectImageFilterConsistent("blur-mirror", image, ImageFilter::Blur(6, 6, TileMode::Mirror),
                              width, height);
  ExpectImageFilterConsistent("blur-decal", image, ImageFilter::Blur(6, 6, TileMode::Decal), width,
                              height);
}

// Color-space conversions into a Display-P3 surface: exercises the color-space operator of
// PointwiseDirectShader and TexturedEffectShader, whose seven pipeline steps are selected by the
// CSFlags runtime uniform. A precompiled variant that reads a flag differently from the runtime
// codegen would show up as a byte mismatch here.
TGFX_TEST(AOTRenderConsistencyTest, ColorSpaceXformModes) {
  int width = 200;
  int height = 200;
  ExpectShaderConsistentP3("csx-srgb-color", Shader::MakeColorShader(Color::Green()), width,
                           height);
  ExpectShaderConsistentP3("csx-srgb-linear",
                           Shader::MakeLinearGradient(Point::Make(0, 0), Point::Make(200, 0),
                                                      {Color::Green(), Color::Red()}, {}),
                           width, height);
  auto image = MakeImage("resources/apitest/mandrill_128.png");
  ASSERT_TRUE(image != nullptr);
  ExpectShaderConsistentP3("csx-srgb-image",
                           Shader::MakeImageShader(image, TileMode::Clamp, TileMode::Clamp), width,
                           height);
}

// Gradient layouts (linear/radial/conic/diamond) are now selected by the LayoutType runtime uniform
// instead of a compile-time dimension. Each layout must still compute t identically to the runtime
// layout FP, so a divergent LayoutType branch or a missing Bias/Scale would show up as a byte
// mismatch here. Two-stop uses SingleInterval, multi-stop uses DualInterval/Texture colorizers.
TGFX_TEST(AOTRenderConsistencyTest, GradientLayoutModes) {
  int width = 200;
  int height = 200;
  Point center = Point::Make(100, 100);
  std::vector<Color> twoStops = {Color::Green(), Color::Red()};
  std::vector<Color> multiStops = {Color::Green(), Color::Blue(), Color::Red()};
  std::vector<float> multiPositions = {0.0f, 0.4f, 1.0f};

  ExpectShaderConsistent(
      "grad-linear",
      Shader::MakeLinearGradient(Point::Make(0, 0), Point::Make(200, 0), twoStops, {}), width,
      height);
  ExpectShaderConsistent("grad-radial", Shader::MakeRadialGradient(center, 100, twoStops, {}),
                         width, height);
  ExpectShaderConsistent("grad-conic", Shader::MakeConicGradient(center, 0, 360, twoStops, {}),
                         width, height);
  ExpectShaderConsistent("grad-diamond", Shader::MakeDiamondGradient(center, 100, twoStops, {}),
                         width, height);
  ExpectShaderConsistent("grad-radial-multi",
                         Shader::MakeRadialGradient(center, 100, multiStops, multiPositions), width,
                         height);
  ExpectShaderConsistent("grad-conic-multi",
                         Shader::MakeConicGradient(center, 0, 360, multiStops, multiPositions),
                         width, height);
}

// Renders an antialiased circle (EllipseGeometryProcessor) under an optional clip into outBitmap.
// clipMode: 0 = no clip, 1 = antialiased clipRect (AARectEffect coverage), 2 = antialiased
// non-rect clipPath (device-space mask texture coverage). This exercises the EllipseFillShader
// HAS_COVERAGE dimension whose mask is sampled in device space via DeviceCoordMatrix * gl_FragCoord;
// a divergent coordinate transform would show up as a byte mismatch against the runtime path.
static void RenderClippedCircleOnce(int clipMode, int width, int height, bool useBundle,
                                    Bitmap* outBitmap) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto* cache = context->precompiledShaderCache();
  if (useBundle) {
    ASSERT_TRUE(cache->loadBundle(ProjectPath::Absolute(ConsistencyBundlePath())));
  } else {
    cache->unload();
  }
  context->globalCache()->clearPrograms();
  auto surface = Surface::Make(context, width, height);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();
  canvas->save();
  if (clipMode == 1) {
    canvas->clipRect(Rect::MakeLTRB(20, 20, 180, 180));
  } else if (clipMode == 2) {
    Path clipPath = {};
    clipPath.addOval(Rect::MakeLTRB(20, 20, 180, 180));
    canvas->clipPath(clipPath);
  }
  Paint paint = {};
  paint.setColor(Color::Red());
  paint.setAntiAlias(true);
  canvas->drawCircle(100, 100, 80, paint);
  canvas->restore();
  context->flushAndSubmit(true);
  ASSERT_TRUE(outBitmap->allocPixels(width, height));
  auto* pixels = outBitmap->lockPixels();
  ASSERT_TRUE(pixels != nullptr);
  ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
  outBitmap->unlockPixels();
  if (useBundle) {
    cache->unload();
  }
}

static void ExpectClippedCircleConsistent(const char* label, int clipMode, int width, int height) {
  Bitmap aotBitmap = {};
  Bitmap runtimeBitmap = {};
  RenderClippedCircleOnce(clipMode, width, height, true, &aotBitmap);
  RenderClippedCircleOnce(clipMode, width, height, false, &runtimeBitmap);
  ExpectBitmapsIdentical(label, aotBitmap, runtimeBitmap, width, height);
}

// EllipseFillShader HAS_COVERAGE dimension: an antialiased circle drawn under a clip pulls in a
// coverage FP (AARectEffect for a rect clip, a device-space mask texture for a non-rect clip). The
// AOT path samples that coverage in device space; this verifies it is byte-identical to the runtime
// ProgramBuilder path, catching any DeviceCoordMatrix / gl_FragCoord misalignment.
TGFX_TEST(AOTRenderConsistencyTest, EllipseFillCoverageModes) {
  int width = 200;
  int height = 200;
  ExpectClippedCircleConsistent("ellipse-no-clip", 0, width, height);
  ExpectClippedCircleConsistent("ellipse-aarect-clip", 1, width, height);
  ExpectClippedCircleConsistent("ellipse-device-mask-clip", 2, width, height);
}

struct ColorFilterRenderStats {
  uint32_t hits = 0;
  uint32_t pipelines = 0;
  uint32_t noMatchingRule = 0;
  AOTDrawStats draws = {};
  ProgramCacheStats programs = {};
};

static void RenderImageWithColorFilterOnce(const std::shared_ptr<Image>& image,
                                           const std::shared_ptr<ColorFilter>& colorFilter,
                                           int width, int height, bool useBundle,
                                           bool decompositionEnabled, bool useAnalyticClip,
                                           bool forceTexture2D, Bitmap* outBitmap,
                                           ColorFilterRenderStats* outStats) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto* cache = context->precompiledShaderCache();
  if (useBundle) {
    auto [bundleData, bundleSize] = EmbeddedShaderBundles::GetBundle(context->backend());
    ASSERT_NE(bundleData, nullptr);
    ASSERT_GT(bundleSize, 0u);
    ASSERT_TRUE(cache->loadBundle(bundleData, bundleSize));
  } else {
    cache->unload();
  }
  cache->setDiagnosticRecordingEnabled(false);
  context->globalCache()->clearPrograms();
  auto sourceImage = image;
  std::shared_ptr<Surface> sourceSurface = nullptr;
  if (forceTexture2D) {
    sourceSurface = Surface::Make(context, width, height, false, 1, true);
    ASSERT_TRUE(sourceSurface != nullptr);
    sourceSurface->getCanvas()->drawImage(image, 0, 0);
    sourceImage = sourceSurface->makeImageSnapshot();
    ASSERT_TRUE(sourceImage != nullptr);
    context->flushAndSubmit(true);
  }
  cache->setDecompositionEnabled(decompositionEnabled);
  cache->setDiagnosticRecordingEnabled(true);
  cache->resetStats();
  context->globalCache()->clearPrograms();
  context->globalCache()->resetProgramStats();
  auto surface = Surface::Make(context, width, height);
  ASSERT_TRUE(surface != nullptr);
  Paint paint = {};
  paint.setColorFilter(colorFilter);
  auto canvas = surface->getCanvas();
  if (useAnalyticClip) {
    canvas->clipRect(Rect::MakeLTRB(8, 8, width - 8, height - 8), true);
  }
  canvas->drawImage(sourceImage, 0, 0, &paint);
  context->flushAndSubmit(true);
  ASSERT_TRUE(outBitmap->allocPixels(width, height));
  auto* pixels = outBitmap->lockPixels();
  ASSERT_TRUE(pixels != nullptr);
  ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
  outBitmap->unlockPixels();
  outStats->hits = cache->hitCount();
  outStats->pipelines = cache->aotStageCount(PrecompiledAOTStage::PipelineCreated);
  outStats->noMatchingRule = cache->fallbackCount(PrecompiledFallbackReason::NoMatchingRule);
  outStats->draws = cache->drawStats();
  outStats->programs = context->globalCache()->programStats();
  cache->setDiagnosticRecordingEnabled(false);
  cache->setDecompositionEnabled(true);
  cache->unload();
  context->globalCache()->clearPrograms();
}

TGFX_TEST(AOTRenderConsistencyTest, TexturedEffect2D) {
  auto image = MakeImage("resources/apitest/mandrill_128.png");
  ASSERT_TRUE(image != nullptr);
  int width = image->width();
  int height = image->height();
  std::array<float, 20> swapRedBlue = {0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0};
  auto colorFilter = ColorFilter::Matrix(swapRedBlue);
  Bitmap reference = {};
  Bitmap candidate = {};
  ColorFilterRenderStats referenceStats = {};
  ColorFilterRenderStats candidateStats = {};
  RenderImageWithColorFilterOnce(image, colorFilter, width, height, false, false, false, true,
                                 &reference, &referenceStats);
  RenderImageWithColorFilterOnce(image, colorFilter, width, height, true, false, false, true,
                                 &candidate, &candidateStats);
  EXPECT_GE(candidateStats.hits, 1u);
  EXPECT_GE(candidateStats.pipelines, 1u);
  EXPECT_GE(candidateStats.programs.precompiledArtifactCreations, 1u);
  EXPECT_EQ(candidateStats.programs.programBuilderCreations, 0u);
  EXPECT_EQ(candidateStats.noMatchingRule, 0u);
  EXPECT_EQ(candidateStats.draws.draws, 1u);
  EXPECT_EQ(candidateStats.draws.completeAOTDraws, 1u);
  EXPECT_EQ(candidateStats.draws.atomicFallbacks, 0u);
  EXPECT_EQ(candidateStats.draws.kernelInvocations, 1u);
  EXPECT_EQ(candidateStats.draws.offscreenTargets, 0u);
  EXPECT_EQ(candidateStats.draws.materializedEdges, 0u);
  ExpectBitmapsIdentical("textured-effect-2d", candidate, reference, width, height);
}

// Three pointwise operators are packed into two fixed-slot passes: the first pass applies Matrix +
// Luma, and the terminal device-space pass applies the final Matrix. This exercises both source
// coordinate domains while requiring only one RGBA8 intermediate.
TGFX_TEST(AOTRenderConsistencyTest, LinearChainMultiPass) {
  auto image = MakeImage("resources/apitest/mandrill_128.png");
  ASSERT_TRUE(image != nullptr);
  int width = image->width();
  int height = image->height();
  std::array<float, 20> swapRedBlue = {0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0};
  std::array<float, 20> swapRedGreen = {0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0};
  auto matrixThenLuma = ColorFilter::Compose(ColorFilter::Matrix(swapRedBlue), ColorFilter::Luma());
  auto chain = ColorFilter::Compose(matrixThenLuma, ColorFilter::Matrix(swapRedGreen));
  ASSERT_TRUE(chain != nullptr);
  Bitmap reference = {};
  Bitmap candidate = {};
  ColorFilterRenderStats referenceStats = {};
  ColorFilterRenderStats candidateStats = {};
  RenderImageWithColorFilterOnce(image, chain, width, height, false, false, false, true, &reference,
                                 &referenceStats);
  RenderImageWithColorFilterOnce(image, chain, width, height, true, true, false, true, &candidate,
                                 &candidateStats);
  EXPECT_GE(candidateStats.hits, 2u);
  EXPECT_GE(candidateStats.pipelines, 2u);
  EXPECT_GE(candidateStats.programs.precompiledArtifactCreations, 2u);
  EXPECT_EQ(candidateStats.programs.programBuilderCreations, 0u);
  EXPECT_EQ(candidateStats.noMatchingRule, 0u);
  auto intermediateBytes = static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 4;
  EXPECT_EQ(candidateStats.draws.draws, 1u);
  EXPECT_EQ(candidateStats.draws.completeAOTDraws, 1u);
  EXPECT_EQ(candidateStats.draws.atomicFallbacks, 0u);
  EXPECT_EQ(candidateStats.draws.kernelInvocations, 2u);
  EXPECT_EQ(candidateStats.draws.offscreenTargets, 1u);
  EXPECT_EQ(candidateStats.draws.materializedEdges, 1u);
  EXPECT_EQ(candidateStats.draws.renderTargetSwitches, 1u);
  EXPECT_EQ(candidateStats.draws.intermediateReadBytes, intermediateBytes);
  EXPECT_EQ(candidateStats.draws.intermediateWriteBytes, intermediateBytes);
  EXPECT_EQ(candidateStats.draws.peakTemporaryBytes, intermediateBytes);
  ExpectBitmapsIdentical("linear-chain-matrix-luma-matrix", candidate, reference, width, height);
}

// Proves AlphaThreshold reaches a fused pointwise slot. The operator was previously rejected by
// AOTPointwiseTailProcessor::Make, so any chain containing it fell back to the runtime path; each
// slot now carries the full operator parameter set.
TGFX_TEST(AOTRenderConsistencyTest, AlphaThresholdChainFusesByteExact) {
  auto image = MakeImage("resources/apitest/mandrill_128.png");
  ASSERT_TRUE(image != nullptr);
  int width = image->width();
  int height = image->height();
  std::array<float, 20> swapRedBlue = {0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0};
  auto chain =
      ColorFilter::Compose(ColorFilter::Matrix(swapRedBlue), ColorFilter::AlphaThreshold(0.25f));
  ASSERT_TRUE(chain != nullptr);
  Bitmap reference = {};
  Bitmap candidate = {};
  ColorFilterRenderStats referenceStats = {};
  ColorFilterRenderStats candidateStats = {};
  RenderImageWithColorFilterOnce(image, chain, width, height, false, false, false, true, &reference,
                                 &referenceStats);
  RenderImageWithColorFilterOnce(image, chain, width, height, true, true, false, true, &candidate,
                                 &candidateStats);
  EXPECT_EQ(candidateStats.programs.programBuilderCreations, 0u);
  EXPECT_EQ(candidateStats.noMatchingRule, 0u);
  // Matrix and AlphaThreshold occupy the two slots of one kernel, so this stays a single pass with
  // no intermediate texture.
  EXPECT_EQ(candidateStats.draws.draws, 1u);
  EXPECT_EQ(candidateStats.draws.atomicFallbacks, 0u);
  EXPECT_EQ(candidateStats.draws.offscreenTargets, 0u);
  ExpectBitmapsIdentical("pointwise-matrix-alphathreshold", candidate, reference, width, height);
}

// Encoded images use GL_TEXTURE_RECTANGLE on macOS. The precompiled textured kernel accepts only
// TextureType::TwoD, so strict preparation must reject the plan before any pass executes and render
// the untouched original draw through the runtime fallback.
TGFX_TEST(AOTRenderConsistencyTest, MultiPassUnsupportedSourceFallsBackAtomically) {
  if (std::string(TGFX_BACKEND_NAME) != "opengl") {
    GTEST_SKIP() << "This case relies on macOS OpenGL encoded images using Rectangle textures";
  }
  auto image = MakeImage("resources/apitest/mandrill_128.png");
  ASSERT_TRUE(image != nullptr);
  int width = image->width();
  int height = image->height();
  std::array<float, 20> swapRedBlue = {0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0};
  auto chain = ColorFilter::Compose(ColorFilter::Matrix(swapRedBlue), ColorFilter::Luma());
  ASSERT_TRUE(chain != nullptr);
  Bitmap reference = {};
  Bitmap candidate = {};
  ColorFilterRenderStats referenceStats = {};
  ColorFilterRenderStats candidateStats = {};
  RenderImageWithColorFilterOnce(image, chain, width, height, false, false, false, false,
                                 &reference, &referenceStats);
  RenderImageWithColorFilterOnce(image, chain, width, height, true, true, false, false, &candidate,
                                 &candidateStats);
  EXPECT_GT(candidateStats.noMatchingRule, 0u);
  EXPECT_EQ(candidateStats.programs.precompiledArtifactCreations, 0u);
  EXPECT_GE(candidateStats.programs.programBuilderCreations, 1u);
  EXPECT_EQ(candidateStats.draws.draws, 1u);
  EXPECT_EQ(candidateStats.draws.completeAOTDraws, 0u);
  EXPECT_EQ(candidateStats.draws.atomicFallbacks, 1u);
  EXPECT_EQ(candidateStats.draws.kernelInvocations, 1u);
  EXPECT_EQ(candidateStats.draws.offscreenTargets, 0u);
  EXPECT_EQ(candidateStats.draws.materializedEdges, 0u);
  EXPECT_EQ(candidateStats.draws.renderTargetSwitches, 0u);
  EXPECT_EQ(candidateStats.draws.intermediateReadBytes, 0u);
  EXPECT_EQ(candidateStats.draws.intermediateWriteBytes, 0u);
  EXPECT_EQ(candidateStats.draws.peakTemporaryBytes, 0u);
  ExpectBitmapsIdentical("linear-chain-unsupported-source-fallback", candidate, reference, width,
                         height);
}

}  // namespace tgfx

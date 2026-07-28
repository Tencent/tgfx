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

// L2 cross-validation audit (design: docs/aot-l2-production-enablement-design.md, stage A).
//
// For every shape the L2 decomposition route can currently serve, render the SAME scene twice:
//   reference = decomposition disabled (plain / JIT path),
//   candidate = decomposition enabled  (L2 path).
// The two must agree within <= 1 LSB, because a single-pass fused shape uses the very same shader
// source on both paths and differs only by artifact origin (precompiled vs JIT). Any per-channel
// difference above 1 is a mapping/uniform-recipe bug, not a tolerance matter, and fails hard.
//
// Each case also emits one machine-readable matrix row:
//   [L2AUDIT] backend=<b> shape=<name> served=<0/1> refHits=<n> candHits=<n> maxDelta=<d> pass=<0/1>
// so a three-backend run yields the shape x backend validation matrix from the design.

#include <string>
#include "base/TGFXTest.h"
#include "core/utils/Log.h"
#include "gpu/GlobalCache.h"
#include "gpu/PrecompiledShaderCache.h"
#include "gtest/gtest.h"
#include "tgfx/core/Bitmap.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/ColorFilter.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/MaskFilter.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Pixmap.h"
#include "tgfx/core/SamplingOptions.h"
#include "tgfx/core/Shader.h"
#include "tgfx/core/Surface.h"
#include "tgfx/gpu/Context.h"
#include "utils/AOTToleranceCompare.h"
#include "utils/TestUtils.h"

namespace tgfx {

#ifndef TGFX_BACKEND_NAME
#define TGFX_BACKEND_NAME "opengl"
#endif

static std::string AuditBundlePath() {
  std::string backend = TGFX_BACKEND_NAME;
  auto pos = backend.find('-');
  if (pos != std::string::npos) {
    backend = backend.substr(0, pos);
  }
  return "resources/shaders/shader_bundle." + backend + ".bin";
}

// Renders one scene and reports its pixels + the AOT artifact hit count. The scene is built by the
// caller-supplied paint so each audit case exercises a different L2-serviceable shape.
static void RenderScene(Context* context, PrecompiledShaderCache* cache,
                        const std::shared_ptr<Image>& image, const Paint& paint, int width,
                        int height, bool decompositionEnabled, Bitmap* outBitmap,
                        uint32_t* outHits) {
  cache->setDecompositionEnabled(decompositionEnabled);
  cache->resetStats();
  context->globalCache()->clearPrograms();
  auto surface = Surface::Make(context, width, height);
  ASSERT_TRUE(surface != nullptr);
  surface->getCanvas()->drawImage(image, 0, 0, &paint);
  context->flushAndSubmit(true);
  ASSERT_TRUE(outBitmap->allocPixels(width, height));
  auto pixels = outBitmap->lockPixels();
  ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
  outBitmap->unlockPixels();
  *outHits = cache->hitCount();
}

// Runs one audit case: renders reference (L2 off) vs candidate (L2 on), enforces the <= 1 LSB
// agreement, and reports whether L2 actually served the draw (candidate hits > reference hits).
static void AuditCase(Context* context, PrecompiledShaderCache* cache,
                      const std::shared_ptr<Image>& image, const Paint& paint, const char* shape) {
  int width = image->width();
  int height = image->height();

  Bitmap referenceBitmap = {};
  uint32_t referenceHits = 0;
  RenderScene(context, cache, image, paint, width, height, false, &referenceBitmap, &referenceHits);

  Bitmap candidateBitmap = {};
  uint32_t candidateHits = 0;
  RenderScene(context, cache, image, paint, width, height, true, &candidateBitmap, &candidateHits);

  Pixmap referencePixmap(referenceBitmap);
  Pixmap candidatePixmap(candidateBitmap);

  // Single-pass fusion shares the shader source across both paths, so only compilation rounding may
  // differ: <= 1 LSB, zero structural difference. This threshold is intentionally strict per design
  // section 5; a larger difference signals an index/recipe mismatch and must fail.
  AOTToleranceSpec spec = {};
  spec.maxChannelDiff = 1;
  spec.maxDiffPixelRatio = 1.0;
  spec.structuralChannelDiff = 2;
  auto result = AOTToleranceCompare::Compare(referencePixmap, candidatePixmap, spec);

  bool served = candidateHits > referenceHits;
  LOGI("[L2AUDIT] backend=%s shape=%s served=%d refHits=%u candHits=%u maxDelta=%d pass=%d",
       TGFX_BACKEND_NAME, shape, served ? 1 : 0, referenceHits, candidateHits,
       result.maxChannelDiff, result.passed ? 1 : 0);

  EXPECT_FALSE(result.sizeMismatch) << shape;
  EXPECT_FALSE(result.structuralDifference) << shape;
  EXPECT_TRUE(result.passed) << shape << " maxDelta=" << result.maxChannelDiff;
}

// Audits the shapes the L2 route serves today: a plain textured image draw (TextureFill kernel) and
// an image with a color-matrix color filter (fused TextureColorMatrix). Each is validated to be
// pixel-equivalent (<= 1 LSB) between the plain and L2 paths.
TGFX_TEST(AOTL2AuditTest, ServedShapesMatchPlainPathWithinOneLSB) {
  auto image = MakeImage("resources/apitest/test_timestretch.png");
  ASSERT_TRUE(image != nullptr);
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto* cache = context->precompiledShaderCache();
  ASSERT_TRUE(cache->loadBundle(ProjectPath::Absolute(AuditBundlePath())));

  {
    Paint paint = {};
    AuditCase(context, cache, image, paint, "TextureFill");
  }
  {
    Paint paint = {};
    std::array<float, 20> matrix = {1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0};
    paint.setColorFilter(ColorFilter::Matrix(matrix));
    AuditCase(context, cache, image, paint, "TextureColorMatrix");
  }

  cache->setDecompositionEnabled(true);  // restore production default (gate on)
  cache->unload();
}

// Renders a full-surface rect painted with the given shader, twice differing only by the
// decomposition gate. Used to exercise blend shaders (the drawImage helper cannot carry a
// paint shader).
static void RenderShaderScene(Context* context, PrecompiledShaderCache* cache,
                              const std::shared_ptr<Shader>& shader, int width, int height,
                              bool decompositionEnabled, Bitmap* outBitmap, uint32_t* outHits,
                              uint32_t* outNoMatch) {
  cache->setDecompositionEnabled(decompositionEnabled);
  cache->resetStats();
  context->globalCache()->clearPrograms();
  auto surface = Surface::Make(context, width, height);
  ASSERT_TRUE(surface != nullptr);
  Paint paint = {};
  paint.setShader(shader);
  surface->getCanvas()->drawRect(Rect::MakeWH(width, height), paint);
  context->flushAndSubmit(true);
  ASSERT_TRUE(outBitmap->allocPixels(width, height));
  auto pixels = outBitmap->lockPixels();
  ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
  outBitmap->unlockPixels();
  *outHits = cache->hitCount();
  *outNoMatch = cache->fallbackCount(PrecompiledFallbackReason::NoMatchingRule);
}

// Measures the L2 error for the primary Xfermode+Tiled target: a blend whose dst child is a
// repeat-tiled image shader (TiledTextureEffect). With decomposition enabled the tiled child is
// materialized to an offscreen texture and the blend collapses to two(TextureEffect,TextureEffect)
// which hits BlendMergeShader. This case quantifies whether that materialization + resample stays
// within tolerance versus sampling the tiled child inline.
TGFX_TEST(AOTL2AuditTest, TiledInBlendMatchesPlainPath) {
  auto image = MakeImage("resources/apitest/test_timestretch.png");
  ASSERT_TRUE(image != nullptr);
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto* cache = context->precompiledShaderCache();
  ASSERT_TRUE(cache->loadBundle(ProjectPath::Absolute(AuditBundlePath())));

  int width = image->width();
  int height = image->height();
  // Decisive test: a plain two-texture Multiply blend (NO tiling, NO flatten). BlendMerge accepts
  // both TextureEffect children inline, so this hits BlendMergeShader when the cache is loaded and
  // falls back to JIT when the cache is unloaded. Comparing the two isolates whether the precompiled
  // BlendMerge kernel's Multiply math matches the runtime/JIT path.
  // Clean L2 test: tiled (repeat, full-size) is the SRC/index-0 child so only the decomposition gate
  // flattens it; the DST child is a same-size plain image so there is no child-size/coord mismatch.
  // Gate OFF: inline two(Tiled,Texture) -> BlendMerge rejects tiled -> JIT (correct reference).
  // Gate ON: tiled materialized full-size -> two(Texture,Texture) -> BlendMerge. Must be bit-exact.
  auto image2 = MakeImage("resources/apitest/test_timestretch.png");
  ASSERT_TRUE(image2 != nullptr);
  auto tiled = Shader::MakeImageShader(image, TileMode::Repeat, TileMode::Repeat);
  auto plain = Shader::MakeImageShader(image2, TileMode::Clamp, TileMode::Clamp);
  ASSERT_TRUE(tiled != nullptr && plain != nullptr);
  auto blend = Shader::MakeBlend(BlendMode::Multiply, plain, tiled);
  ASSERT_TRUE(blend != nullptr);

  Bitmap warmupBitmap = {};
  uint32_t warmupHits = 0;
  uint32_t warmupNoMatch = 0;
  RenderShaderScene(context, cache, blend, width, height, false, &warmupBitmap, &warmupHits,
                    &warmupNoMatch);

  // Reference = decomposition OFF (tiled inline -> JIT).
  Bitmap referenceBitmap = {};
  uint32_t referenceHits = 0;
  uint32_t referenceNoMatch = 0;
  RenderShaderScene(context, cache, blend, width, height, false, &referenceBitmap, &referenceHits,
                    &referenceNoMatch);
  // Candidate = decomposition ON (tiled materialized -> BlendMerge).
  Bitmap candidateBitmap = {};
  uint32_t candidateHits = 0;
  uint32_t candidateNoMatch = 0;
  RenderShaderScene(context, cache, blend, width, height, true, &candidateBitmap, &candidateHits,
                    &candidateNoMatch);

  Pixmap referencePixmap(referenceBitmap);
  Pixmap candidatePixmap(candidateBitmap);
  SaveImage(referencePixmap, "AOTL2AuditTest/BlendMergeMultiply_jit");
  SaveImage(candidatePixmap, "AOTL2AuditTest/BlendMergeMultiply_aot");
  AOTToleranceSpec spec = {};
  spec.maxChannelDiff = 1;
  spec.maxDiffPixelRatio = 1.0;
  spec.structuralChannelDiff = 2;
  auto result = AOTToleranceCompare::Compare(referencePixmap, candidatePixmap, spec);
  LOGI(
      "[L2AUDIT] backend=%s shape=BlendMergeMultiply jitHits=%u aotHits=%u maxDelta=%d "
      "diffPixels=%d "
      "totalPixels=%d pass=%d",
      TGFX_BACKEND_NAME, referenceHits, candidateHits, result.maxChannelDiff,
      static_cast<int>(result.diffPixelCount), static_cast<int>(result.totalPixelCount),
      result.passed ? 1 : 0);

  cache->setDecompositionEnabled(true);  // restore production default (gate on)
  cache->unload();

  EXPECT_TRUE(result.passed) << "AOT BlendMerge Multiply diverges from JIT, maxDelta="
                             << result.maxChannelDiff;
}

// Renders the DropShadow-with-tiled-source scene (a gradient masked by a Decal image shader, with a
// drop-shadow image filter). The Decal mask shader becomes a TiledTextureEffect that DropShadow
// composes with the shadow via Xfermode-two, i.e. the two(TiledTextureEffect,TextureEffect) case.
static void RenderDropShadowTiledScene(Context* context, PrecompiledShaderCache* cache,
                                       const std::shared_ptr<Image>& image, int width, int height,
                                       bool decompositionEnabled, Bitmap* outBitmap,
                                       uint32_t* outNoMatch) {
  cache->setDecompositionEnabled(decompositionEnabled);
  cache->resetStats();
  context->globalCache()->clearPrograms();
  auto surface = Surface::Make(context, width, height);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();
  Paint paint = {};
  paint.setShader(Shader::MakeRadialGradient({width / 2.0f, height / 2.0f}, width / 2.0f,
                                             {Color::Green(), Color::Blue()}, {}));
  auto maskShader = Shader::MakeImageShader(image, TileMode::Decal, TileMode::Decal);
  paint.setMaskFilter(MaskFilter::MakeShader(maskShader));
  paint.setImageFilter(ImageFilter::DropShadow(-10, -10, 10, 10, Color::Black()));
  canvas->drawRect(Rect::MakeWH(width, height), paint);
  context->flushAndSubmit(true);
  ASSERT_TRUE(outBitmap->allocPixels(width, height));
  auto pixels = outBitmap->lockPixels();
  ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
  outBitmap->unlockPixels();
  *outNoMatch = cache->fallbackCount(PrecompiledFallbackReason::NoMatchingRule);
}

// A-color quantification: the drop-shadow-over-tiled-source draw produces a
// two(TiledTextureEffect,TextureEffect) blend that misses inline (NoMatchingRule). With the
// decomposition gate on, DropShadow's EnsureSimpleBlendChild materializes the tiled source, the
// blend collapses to two(TextureEffect,TextureEffect) and hits BlendMerge. Verifies the miss is
// eliminated AND the result stays pixel-identical. Uses explicit gate off/on on one context and
// restores the gate default at the end (the test context is shared process-wide).
TGFX_TEST(AOTL2AuditTest, DropShadowTiledSrcMaterializes) {
  auto image = MakeImage("resources/apitest/imageReplacement.png");
  ASSERT_TRUE(image != nullptr);
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto* cache = context->precompiledShaderCache();
  ASSERT_TRUE(cache->loadBundle(ProjectPath::Absolute(AuditBundlePath())));

  int width = 160;
  int height = 160;
  Bitmap warmupBitmap = {};
  uint32_t warmupNoMatch = 0;
  RenderDropShadowTiledScene(context, cache, image, width, height, false, &warmupBitmap,
                             &warmupNoMatch);

  Bitmap referenceBitmap = {};
  uint32_t referenceNoMatch = 0;
  RenderDropShadowTiledScene(context, cache, image, width, height, false, &referenceBitmap,
                             &referenceNoMatch);
  Bitmap candidateBitmap = {};
  uint32_t candidateNoMatch = 0;
  RenderDropShadowTiledScene(context, cache, image, width, height, true, &candidateBitmap,
                             &candidateNoMatch);

  Pixmap referencePixmap(referenceBitmap);
  Pixmap candidatePixmap(candidateBitmap);
  AOTToleranceSpec spec = {};
  spec.maxChannelDiff = 1;
  spec.maxDiffPixelRatio = 1.0;
  spec.structuralChannelDiff = 2;
  auto result = AOTToleranceCompare::Compare(referencePixmap, candidatePixmap, spec);
  LOGI(
      "[L2AUDIT] backend=%s shape=DropShadowTiled refNoMatch=%u candNoMatch=%u maxDelta=%d pass=%d",
      TGFX_BACKEND_NAME, referenceNoMatch, candidateNoMatch, result.maxChannelDiff,
      result.passed ? 1 : 0);

  cache->setDecompositionEnabled(true);  // restore production default (gate on)
  cache->unload();

  EXPECT_LT(candidateNoMatch, referenceNoMatch)
      << "decomposition did not reduce the tiled-in-blend NoMatchingRule fallback";
  EXPECT_TRUE(result.passed) << "materialized drop-shadow diverges, maxDelta="
                             << result.maxChannelDiff;
}

}  // namespace tgfx

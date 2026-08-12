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

// L2 cross-validation audit.
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
#include "core/utils/BlockAllocator.h"
#include "core/utils/Log.h"
#include "gpu/GlobalCache.h"
#include "gpu/PrecompiledShaderCache.h"
#include "gpu/ProxyProvider.h"
#include "gpu/SamplingArgs.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/processors/TextureEffect.h"
#include "gtest/gtest.h"
#include "tgfx/core/Bitmap.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/ColorFilter.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/MaskFilter.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/PictureRecorder.h"
#include "tgfx/core/Pixmap.h"
#include "tgfx/core/SamplingOptions.h"
#include "tgfx/core/Shader.h"
#include "tgfx/core/Surface.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/gpu/PixelFormat.h"
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

static bool IsOpenGLAuditBackend() {
  std::string backend = TGFX_BACKEND_NAME;
  return backend.rfind("opengl", 0) == 0;
}

// Renders one scene and reports its pixels + the AOT artifact hit count. The scene is built by the
// caller-supplied paint so each audit case exercises a different L2-serviceable shape.
static void RenderScene(Context* context, PrecompiledShaderCache* cache,
                        const std::shared_ptr<Image>& image, const Paint& paint, int width,
                        int height, bool decompositionEnabled, Bitmap* outBitmap,
                        uint32_t* outHits) {
  cache->setDecompositionEnabled(decompositionEnabled);
  cache->resetStats();
  context->globalCache()->resetProgramStats();
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
  context->globalCache()->resetProgramStats();
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

static void RenderShaderWithColorFilterScene(Context* context, PrecompiledShaderCache* cache,
                                             const std::shared_ptr<Shader>& shader,
                                             const std::shared_ptr<ColorFilter>& colorFilter,
                                             int width, int height, bool decompositionEnabled,
                                             bool bundleLoaded, Bitmap* outBitmap,
                                             bool* pointwiseChainHit, uint32_t* outNoMatch) {
  cache->setDecompositionEnabled(decompositionEnabled);
  if (bundleLoaded) {
    ASSERT_TRUE(cache->loadBundle(ProjectPath::Absolute(AuditBundlePath())));
  } else {
    cache->unload();
  }
  ScopedAOTStatsPause statsPause(context, !bundleLoaded);
  cache->resetStats();
  context->globalCache()->resetProgramStats();
  context->globalCache()->clearPrograms();
  auto surface = Surface::Make(context, width, height);
  ASSERT_TRUE(surface != nullptr);
  Paint paint = {};
  paint.setShader(shader);
  paint.setColorFilter(colorFilter);
  surface->getCanvas()->drawRect(
      Rect::MakeWH(static_cast<float>(width), static_cast<float>(height)), paint);
  context->flushAndSubmit(true);
  ASSERT_TRUE(outBitmap->allocPixels(width, height));
  auto pixels = outBitmap->lockPixels();
  ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
  outBitmap->unlockPixels();
  *pointwiseChainHit = false;
  for (const auto& record : cache->hitRecords()) {
    *pointwiseChainHit = *pointwiseChainHit || record.shaderName == "PointwiseChainShader";
  }
  *outNoMatch = cache->fallbackCount(PrecompiledFallbackReason::NoMatchingRule);
  cache->unload();
}

// Measures the L2 error for the primary Xfermode+Tiled target: a blend whose dst child is a
// repeat-tiled image shader (TiledTextureEffect). With decomposition enabled the tiled child is
// materialized to an offscreen texture and the blend collapses to two(TextureEffect,TextureEffect)
// which hits the pointwise chain kernel. This case quantifies whether that materialization + resample stays
// within tolerance versus sampling the tiled child inline.
static std::shared_ptr<Image> MakeAlphaOnlyAuditImage() {
  Bitmap bitmap = {};
  if (!bitmap.allocPixels(64, 64, true)) {
    return nullptr;
  }
  auto pixels = static_cast<uint8_t*>(bitmap.lockPixels());
  if (pixels == nullptr) {
    bitmap.unlockPixels();
    return nullptr;
  }
  auto rowBytes = bitmap.rowBytes();
  for (size_t y = 0; y < 64; ++y) {
    for (size_t x = 0; x < 64; ++x) {
      pixels[y * rowBytes + x] = static_cast<uint8_t>((x * 3 + y * 5) % 256);
    }
  }
  bitmap.unlockPixels();
  return Image::MakeFrom(bitmap);
}

static std::shared_ptr<Image> MakeColorAuditImage() {
  Bitmap bitmap = {};
  if (!bitmap.allocPixels(64, 64, false)) {
    return nullptr;
  }
  auto pixels = static_cast<uint32_t*>(bitmap.lockPixels());
  if (pixels == nullptr) {
    bitmap.unlockPixels();
    return nullptr;
  }
  for (size_t y = 0; y < 64; ++y) {
    for (size_t x = 0; x < 64; ++x) {
      auto r = static_cast<uint32_t>((x * 5) % 256);
      auto g = static_cast<uint32_t>((y * 7) % 256);
      auto b = static_cast<uint32_t>((x * 3 + y * 2) % 256);
      pixels[y * 64 + x] = (255u << 24) | (b << 16) | (g << 8) | r;
    }
  }
  bitmap.unlockPixels();
  return Image::MakeFrom(bitmap);
}

static void ExpectChainAlphaOnlyExact(const std::shared_ptr<Shader>& shader, const char* label,
                                      Context* context, PrecompiledShaderCache* cache, int width,
                                      int height) {
  Bitmap reference = {};
  Bitmap candidate = {};
  uint32_t referenceHits = 0;
  uint32_t referenceNoMatch = 0;
  uint32_t candidateHits = 0;
  uint32_t candidateNoMatch = 0;
  // The blend draw routes to the fused pointwise chain kernel; this proves the chain's alpha-only
  // child handling stays byte-exact against the JIT path.
  RenderShaderScene(context, cache, shader, width, height, false, &candidate, &candidateHits,
                    &candidateNoMatch);
  auto hits = cache->hitRecords();
  bool chainHit = false;
  for (const auto& hit : hits) {
    chainHit = chainHit || hit.shaderName == "PointwiseChainShader";
  }
  EXPECT_TRUE(chainHit) << label;
  cache->unload();
  {
    // The reference render intentionally runs without the bundle; keep its JIT lookups out of the
    // AOT hit-rate accounting.
    ScopedAOTStatsPause statsPause(context, true);
    RenderShaderScene(context, cache, shader, width, height, false, &reference, &referenceHits,
                      &referenceNoMatch);
  }
  EXPECT_GT(candidateHits, 0u) << label;
  EXPECT_EQ(candidateNoMatch, 0u) << label;
  Pixmap referencePixmap(reference);
  Pixmap candidatePixmap(candidate);
  AOTToleranceSpec spec = {};
  spec.maxChannelDiff = 0;
  spec.maxDiffPixelRatio = 1.0;
  spec.structuralChannelDiff = 2;
  auto result = AOTToleranceCompare::Compare(referencePixmap, candidatePixmap, spec);
  EXPECT_TRUE(result.passed) << label << " maxDelta=" << result.maxChannelDiff
                             << " diffPixels=" << result.diffPixelCount;
}

TGFX_TEST(AOTL2AuditTest, ChainAlphaOnlyChildrenMatchPlainPath) {
  auto alphaImage = MakeAlphaOnlyAuditImage();
  auto colorImage = MakeColorAuditImage();
  ASSERT_TRUE(alphaImage != nullptr && colorImage != nullptr);
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto* cache = context->precompiledShaderCache();
  ASSERT_TRUE(cache->loadBundle(ProjectPath::Absolute(AuditBundlePath())));
  auto alpha = Shader::MakeImageShader(alphaImage, TileMode::Clamp, TileMode::Clamp);
  auto color = Shader::MakeImageShader(colorImage, TileMode::Clamp, TileMode::Clamp);
  ASSERT_TRUE(alpha != nullptr && color != nullptr);
  int width = 64;
  int height = 64;

  // BlendShader::asFragmentProcessor registers child[0] as src and child[1] as dst.
  auto srcAlphaDstColor = Shader::MakeBlend(BlendMode::Multiply, color, alpha);
  auto srcColorDstAlpha = Shader::MakeBlend(BlendMode::Multiply, alpha, color);
  ASSERT_TRUE(srcAlphaDstColor != nullptr && srcColorDstAlpha != nullptr);
  // MakeBlend collapses null operands for modes where the result is independent of the missing side;
  // use explicit two-child cases for the alpha semantic matrix below.
  ASSERT_TRUE(cache->loadBundle(ProjectPath::Absolute(AuditBundlePath())));
  ExpectChainAlphaOnlyExact(srcAlphaDstColor, "two-child-src-color-dst-alpha", context, cache,
                            width, height);
  ASSERT_TRUE(cache->loadBundle(ProjectPath::Absolute(AuditBundlePath())));
  ExpectChainAlphaOnlyExact(srcColorDstAlpha, "two-child-src-alpha-dst-color", context, cache,
                            width, height);
}

TGFX_TEST(AOTL2AuditTest, CleanBlendPrefersPointwiseChain) {
  auto imageA = MakeImage("resources/apitest/test_timestretch.png");
  auto imageB = MakeImage("resources/apitest/mandrill_128.png");
  ASSERT_TRUE(imageA != nullptr && imageB != nullptr);
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto* cache = context->precompiledShaderCache();
  ASSERT_TRUE(cache->loadBundle(ProjectPath::Absolute(AuditBundlePath())));
  auto dst = Shader::MakeImageShader(imageA, TileMode::Clamp, TileMode::Clamp);
  auto src = Shader::MakeImageShader(imageB, TileMode::Clamp, TileMode::Clamp);
  auto blend = Shader::MakeBlend(BlendMode::Multiply, dst, src);
  ASSERT_TRUE(blend != nullptr);
  Bitmap candidate = {};
  uint32_t hits = 0;
  uint32_t noMatch = 0;
  RenderShaderScene(context, cache, blend, imageA->width(), imageA->height(), true, &candidate,
                    &hits, &noMatch);
  EXPECT_EQ(noMatch, 0u);
  bool chainHit = false;
  for (const auto& record : cache->hitRecords()) {
    chainHit = chainHit || record.shaderName == "PointwiseChainShader";
  }
  EXPECT_TRUE(chainHit);
}

TGFX_TEST(AOTL2AuditTest, TextureBlendColorFilterConstColorUsesPointwiseChain) {
  auto image = MakeColorAuditImage();
  ASSERT_TRUE(image != nullptr);
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto* cache = context->precompiledShaderCache();
  auto shader = Shader::MakeImageShader(image, TileMode::Clamp, TileMode::Clamp);
  ASSERT_TRUE(shader != nullptr);
  auto colorFilter = ColorFilter::Blend(Color{0.73f, 0.21f, 0.61f, 0.5f}, BlendMode::Multiply);
  ASSERT_TRUE(colorFilter != nullptr);
  Bitmap reference = {};
  Bitmap candidate = {};
  bool chainHit = false;
  uint32_t noMatch = 0;
  RenderShaderWithColorFilterScene(context, cache, shader, colorFilter, 64, 64, false, false,
                                   &reference, &chainHit, &noMatch);
  RenderShaderWithColorFilterScene(context, cache, shader, colorFilter, 64, 64, true, true,
                                   &candidate, &chainHit, &noMatch);
  EXPECT_TRUE(chainHit);
  EXPECT_EQ(noMatch, 0u);
  Pixmap referencePixmap(reference);
  Pixmap candidatePixmap(candidate);
  AOTToleranceSpec spec = {};
  spec.maxChannelDiff = 1;
  spec.maxDiffPixelRatio = 1.0;
  spec.structuralChannelDiff = 2;
  auto result = AOTToleranceCompare::Compare(referencePixmap, candidatePixmap, spec);
  EXPECT_TRUE(result.passed) << "maxDelta=" << result.maxChannelDiff
                             << " diffPixels=" << result.diffPixelCount;
}

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
  // Decisive test: a plain two-texture Multiply blend (NO tiling, NO flatten). The chain
  // accepts both TextureEffect children inline, so this hits PointwiseChainShader when loaded and
  // falls back to JIT when the cache is unloaded. Comparing the two isolates whether the precompiled
  // chain kernel's Multiply math matches the runtime/JIT path.
  // Clean L2 test: tiled (repeat, full-size) is the SRC/index-0 child so only the decomposition gate
  // flattens it; the DST child is a same-size plain image so there is no child-size/coord mismatch.
  // Gate OFF: inline two(Tiled,Texture) -> the chain rejects tiled -> JIT (correct reference).
  // Gate ON: tiled materialized full-size -> two(Texture,Texture) -> chain. Must be bit-exact.
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
static void RenderShadowTiledScene(Context* context, PrecompiledShaderCache* cache,
                                   const std::shared_ptr<Image>& image,
                                   const std::shared_ptr<ImageFilter>& imageFilter, int width,
                                   int height, bool decompositionEnabled, Bitmap* outBitmap,
                                   uint32_t* outNoMatch, uint32_t* outVertexArtifactMissing,
                                   uint32_t* outFragmentArtifactMissing) {
  cache->setDecompositionEnabled(decompositionEnabled);
  cache->resetStats();
  context->globalCache()->resetProgramStats();
  context->globalCache()->clearPrograms();
  auto surface = Surface::Make(context, width, height);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();
  Paint paint = {};
  paint.setShader(Shader::MakeRadialGradient({width / 2.0f, height / 2.0f}, width / 2.0f,
                                             {Color::Green(), Color::Blue()}, {}));
  auto maskShader = Shader::MakeImageShader(image, TileMode::Decal, TileMode::Decal);
  paint.setMaskFilter(MaskFilter::MakeShader(maskShader));
  paint.setImageFilter(imageFilter);
  canvas->drawRect(Rect::MakeWH(width, height), paint);
  context->flushAndSubmit(true);
  ASSERT_TRUE(outBitmap->allocPixels(width, height));
  auto pixels = outBitmap->lockPixels();
  ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
  outBitmap->unlockPixels();
  *outNoMatch = cache->fallbackCount(PrecompiledFallbackReason::NoMatchingRule);
  *outVertexArtifactMissing =
      cache->fallbackCount(PrecompiledFallbackReason::VertexArtifactMissing);
  *outFragmentArtifactMissing =
      cache->fallbackCount(PrecompiledFallbackReason::FragmentArtifactMissing);
}

// DropShadow keeps its original nested Xfermode/TiledTexture processor tree. With the gradient
// chain op the complete tree resolves to precompiled programs, and the result must be
// byte-identical to the no-bundle reference render.
TGFX_TEST(AOTL2AuditTest, DropShadowTiledSrcServedByteExact) {
  auto image = MakeImage("resources/apitest/imageReplacement.png");
  ASSERT_TRUE(image != nullptr);
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto* cache = context->precompiledShaderCache();
  int width = 160;
  int height = 160;
  auto imageFilter = ImageFilter::DropShadow(-10, -10, 10, 10, Color::FromRGBA(0, 255, 0, 128));
  ASSERT_TRUE(imageFilter != nullptr);

  cache->unload();
  Bitmap referenceBitmap = {};
  uint32_t referenceNoMatch = 0;
  uint32_t referenceVertexArtifactMissing = 0;
  uint32_t referenceFragmentArtifactMissing = 0;
  {
    ScopedAOTStatsPause statsPause(context, true);
    RenderShadowTiledScene(context, cache, image, imageFilter, width, height, false,
                           &referenceBitmap, &referenceNoMatch, &referenceVertexArtifactMissing,
                           &referenceFragmentArtifactMissing);
  }

  ASSERT_TRUE(cache->loadBundle(ProjectPath::Absolute(AuditBundlePath())));
  Bitmap candidateBitmap = {};
  uint32_t candidateNoMatch = 0;
  uint32_t candidateVertexArtifactMissing = 0;
  uint32_t candidateFragmentArtifactMissing = 0;
  RenderShadowTiledScene(context, cache, image, imageFilter, width, height, true, &candidateBitmap,
                         &candidateNoMatch, &candidateVertexArtifactMissing,
                         &candidateFragmentArtifactMissing);

  Pixmap referencePixmap(referenceBitmap);
  Pixmap candidatePixmap(candidateBitmap);
  AOTToleranceSpec spec = {};
  auto result = AOTToleranceCompare::Compare(referencePixmap, candidatePixmap, spec);
  LOGI(
      "[L2AUDIT] backend=%s shape=DropShadowTiledJITFallback refNoMatch=%u candNoMatch=%u "
      "diffPixels=%llu maxDelta=%d pass=%d",
      TGFX_BACKEND_NAME, referenceNoMatch, candidateNoMatch,
      static_cast<unsigned long long>(result.diffPixelCount), result.maxChannelDiff,
      result.passed ? 1 : 0);

  cache->setDecompositionEnabled(true);  // restore production default (gate on)
  cache->unload();
  context->globalCache()->clearPrograms();

  EXPECT_EQ(candidateNoMatch, 0u) << "expected the complete DropShadow tree to be served by AOT";
  EXPECT_EQ(candidateVertexArtifactMissing, 0u);
  EXPECT_EQ(candidateFragmentArtifactMissing, 0u);
  EXPECT_FALSE(result.sizeMismatch);
  EXPECT_EQ(result.diffPixelCount, 0u);
  EXPECT_EQ(result.maxChannelDiff, 0);
  EXPECT_TRUE(result.passed) << "DropShadow AOT render diverges from the no-bundle reference";
}

// InnerShadow preserves its nested SrcOut inside SrcATop/SrcIn tree. With the gradient chain op
// the complete tree resolves to precompiled programs, byte-identical to the no-bundle reference.
TGFX_TEST(AOTL2AuditTest, InnerShadowTiledSrcServedByteExact) {
  auto image = MakeImage("resources/apitest/imageReplacement.png");
  ASSERT_TRUE(image != nullptr);
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto* cache = context->precompiledShaderCache();
  int width = 160;
  int height = 160;
  auto imageFilter = ImageFilter::InnerShadow(0, -10.5f, 2, 2, Color::FromRGBA(0, 255, 255, 128));
  ASSERT_TRUE(imageFilter != nullptr);

  cache->unload();
  Bitmap referenceBitmap = {};
  uint32_t referenceNoMatch = 0;
  uint32_t referenceVertexArtifactMissing = 0;
  uint32_t referenceFragmentArtifactMissing = 0;
  {
    ScopedAOTStatsPause statsPause(context, true);
    RenderShadowTiledScene(context, cache, image, imageFilter, width, height, false,
                           &referenceBitmap, &referenceNoMatch, &referenceVertexArtifactMissing,
                           &referenceFragmentArtifactMissing);
  }

  ASSERT_TRUE(cache->loadBundle(ProjectPath::Absolute(AuditBundlePath())));
  Bitmap candidateBitmap = {};
  uint32_t candidateNoMatch = 0;
  uint32_t candidateVertexArtifactMissing = 0;
  uint32_t candidateFragmentArtifactMissing = 0;
  RenderShadowTiledScene(context, cache, image, imageFilter, width, height, true, &candidateBitmap,
                         &candidateNoMatch, &candidateVertexArtifactMissing,
                         &candidateFragmentArtifactMissing);

  Pixmap referencePixmap(referenceBitmap);
  Pixmap candidatePixmap(candidateBitmap);
  AOTToleranceSpec spec = {};
  auto result = AOTToleranceCompare::Compare(referencePixmap, candidatePixmap, spec);
  LOGI(
      "[L2AUDIT] backend=%s shape=InnerShadowTiledJITFallback refNoMatch=%u candNoMatch=%u "
      "diffPixels=%llu maxDelta=%d pass=%d",
      TGFX_BACKEND_NAME, referenceNoMatch, candidateNoMatch,
      static_cast<unsigned long long>(result.diffPixelCount), result.maxChannelDiff,
      result.passed ? 1 : 0);

  cache->setDecompositionEnabled(true);
  cache->unload();
  context->globalCache()->clearPrograms();

  EXPECT_EQ(candidateNoMatch, 0u) << "expected the complete InnerShadow tree to be served by AOT";
  EXPECT_EQ(candidateVertexArtifactMissing, 0u);
  EXPECT_EQ(candidateFragmentArtifactMissing, 0u);
  EXPECT_FALSE(result.sizeMismatch);
  EXPECT_EQ(result.diffPixelCount, 0u);
  EXPECT_EQ(result.maxChannelDiff, 0);
  EXPECT_TRUE(result.passed) << "InnerShadow AOT render diverges from the no-bundle reference";
}

static void DrawDecalShaderMaskScene(Canvas* canvas, const std::shared_ptr<Image>& color,
                                     const std::shared_ptr<Image>& mask, bool inverted) {
  Paint paint = {};
  paint.setShader(Shader::MakeImageShader(color, TileMode::Clamp, TileMode::Clamp));
  auto maskShader = Shader::MakeImageShader(mask, TileMode::Decal, TileMode::Decal);
  paint.setMaskFilter(MaskFilter::MakeShader(maskShader, inverted));
  canvas->save();
  canvas->translate(17.25f, 19.75f);
  canvas->drawRect(Rect::MakeWH(140, 120), paint);
  canvas->restore();
}

static void RenderDecalShaderMaskScene(Context* context, PrecompiledShaderCache* cache,
                                       const std::shared_ptr<Image>& color,
                                       const std::shared_ptr<Image>& mask, bool inverted,
                                       bool usePicture, bool decompositionEnabled,
                                       Bitmap* outBitmap, uint32_t* outNoMatch) {
  cache->setDecompositionEnabled(decompositionEnabled);
  cache->resetStats();
  context->globalCache()->resetProgramStats();
  context->globalCache()->clearPrograms();
  auto surface = Surface::Make(context, 180, 160);
  ASSERT_TRUE(surface != nullptr);
  if (usePicture) {
    PictureRecorder recorder = {};
    DrawDecalShaderMaskScene(recorder.beginRecording(), color, mask, inverted);
    surface->getCanvas()->drawPicture(recorder.finishRecordingAsPicture());
  } else {
    DrawDecalShaderMaskScene(surface->getCanvas(), color, mask, inverted);
  }
  context->flushAndSubmit(true);
  ASSERT_TRUE(outBitmap->allocPixels(surface->width(), surface->height()));
  auto pixels = outBitmap->lockPixels();
  ASSERT_TRUE(pixels != nullptr);
  ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
  outBitmap->unlockPixels();
  *outNoMatch = cache->fallbackCount(PrecompiledFallbackReason::NoMatchingRule);
}

TGFX_TEST(AOTL2AuditTest, ShaderMaskDecalFallsBackByteExact) {
  auto color = MakeImage("resources/apitest/mandrill_128.png");
  auto mask = MakeImage("resources/apitest/imageReplacement.png");
  ASSERT_TRUE(color != nullptr && mask != nullptr);
  ASSERT_FALSE(mask->isAlphaOnly());
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto* cache = context->precompiledShaderCache();
  ASSERT_TRUE(cache->loadBundle(ProjectPath::Absolute(AuditBundlePath())));

  for (bool usePicture : {false, true}) {
    for (bool inverted : {false, true}) {
      Bitmap reference = {};
      Bitmap candidate = {};
      uint32_t referenceNoMatch = 0;
      uint32_t candidateNoMatch = 0;
      RenderDecalShaderMaskScene(context, cache, color, mask, inverted, usePicture, false,
                                 &reference, &referenceNoMatch);
      RenderDecalShaderMaskScene(context, cache, color, mask, inverted, usePicture, true,
                                 &candidate, &candidateNoMatch);
      Pixmap referencePixmap(reference);
      Pixmap candidatePixmap(candidate);
      AOTToleranceSpec spec = {};
      auto result = AOTToleranceCompare::Compare(referencePixmap, candidatePixmap, spec);
      EXPECT_GT(referenceNoMatch, 0u);
      EXPECT_GT(candidateNoMatch, 0u);
      EXPECT_FALSE(result.sizeMismatch);
      EXPECT_EQ(result.diffPixelCount, 0u);
      EXPECT_EQ(result.maxChannelDiff, 0);
      EXPECT_TRUE(result.passed);
    }
  }

  cache->setDecompositionEnabled(true);
  cache->unload();
  context->globalCache()->clearPrograms();
}

// Renders an image (color = TextureEffect) masked by an image-shader mask filter. ShaderMaskFilter
// wraps the mask shader via MulInputByChildAlpha -> XfermodeFragmentProcessor-dst, so the coverage
// FP is CoverageFP=[Xfermode-dst(TextureEffect)] — the local-space texture-alpha mask class served
// by HAS_LOCAL_MASK. A Clamp-tiled mask resolves to hardware sampling on every backend, so the
// TextureEffect/TiledTextureEffect child takes the local-mask path deterministically.
static void RenderCoverageMaskScene(Context* context, const std::shared_ptr<Image>& color,
                                    const std::shared_ptr<Image>& mask, int width, int height,
                                    Bitmap* outBitmap) {
  context->globalCache()->clearPrograms();
  auto surface = Surface::Make(context, width, height);
  ASSERT_TRUE(surface != nullptr);
  Paint paint = {};
  paint.setShader(Shader::MakeImageShader(color, TileMode::Clamp, TileMode::Clamp));
  auto maskShader = Shader::MakeImageShader(mask, TileMode::Clamp, TileMode::Clamp);
  paint.setMaskFilter(MaskFilter::MakeShader(maskShader));
  surface->getCanvas()->drawRect(Rect::MakeWH(width, height), paint);
  context->flushAndSubmit(true);
  ASSERT_TRUE(outBitmap->allocPixels(width, height));
  auto pixels = outBitmap->lockPixels();
  ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
  outBitmap->unlockPixels();
}

// Validates the HAS_LOCAL_MASK path: an image draw with an image-shader mask filter produces
// CoverageFP=[Xfermode-dst(TextureEffect)], the local-space texture-alpha mask class. With the
// precompiled bundle loaded this must (1) hit a precompiled artifact (zero NoMatchingRule) and
// (2) render identically to the JIT/ProgramBuilder path, proving the hand-written coverage sampling
// matches the fragment-processor chain and that the shared-block uniforms are not aliased.
TGFX_TEST(AOTL2AuditTest, CoverageTextureMaskMatchesJIT) {
  auto color = MakeImage("resources/apitest/imageReplacement.png");
  auto mask = MakeImage("resources/apitest/test_timestretch.png");
  ASSERT_TRUE(color != nullptr && mask != nullptr);
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto* cache = context->precompiledShaderCache();

  int width = 160;
  int height = 160;

  // Reference: cache not loaded -> ProgramBuilder (JIT) path.
  cache->unload();
  cache->resetStats();
  context->globalCache()->resetProgramStats();
  Bitmap referenceBitmap = {};
  {
    ScopedAOTStatsPause statsPause(context, true);
    RenderCoverageMaskScene(context, color, mask, width, height, &referenceBitmap);
  }

  // Candidate: cache loaded -> precompiled artifact path.
  ASSERT_TRUE(cache->loadBundle(ProjectPath::Absolute(AuditBundlePath())));
  cache->resetStats();
  context->globalCache()->resetProgramStats();
  Bitmap candidateBitmap = {};
  RenderCoverageMaskScene(context, color, mask, width, height, &candidateBitmap);
  uint32_t noMatch = cache->fallbackCount(PrecompiledFallbackReason::NoMatchingRule);
  uint32_t hits = cache->hitCount();
  cache->unload();

  Pixmap referencePixmap(referenceBitmap);
  Pixmap candidatePixmap(candidateBitmap);
  AOTToleranceSpec spec = {};
  spec.maxChannelDiff = 1;
  spec.maxDiffPixelRatio = 1.0;
  spec.structuralChannelDiff = 2;
  auto result = AOTToleranceCompare::Compare(referencePixmap, candidatePixmap, spec);
  LOGI("[L2COV] backend=%s shape=CoverageTextureMask noMatch=%u hits=%u maxDelta=%d pass=%d",
       TGFX_BACKEND_NAME, noMatch, hits, result.maxChannelDiff, result.passed ? 1 : 0);

  if (IsOpenGLAuditBackend()) {
    // The first desktop OpenGL profile only admits unmasked Texture2D pointwise draws. Coverage
    // texture masks stay on ProgramBuilder until their OpenGL permutation is pixel-validated.
    EXPECT_GT(noMatch, 0u);
    EXPECT_EQ(hits, 0u);
  } else {
    EXPECT_EQ(noMatch, 0u)
        << "coverage Xfermode-dst(TextureEffect) still misses the precompiled cache";
    EXPECT_GT(hits, 0u) << "expected a precompiled artifact hit for the local-mask coverage draw";
  }
  EXPECT_FALSE(result.structuralDifference);
  EXPECT_TRUE(result.passed) << "AOT local-mask coverage diverges from JIT, maxDelta="
                             << result.maxChannelDiff;
}

// Walks a fragment-processor tree in the same pre-order the GP uses when it assigns coordTransform
// indices, logging each node. Accumulates, via *coordCursor, the running global coordTransform index
// (shared across the color and coverage forests exactly as ProgramInfo's CoordTransformIter does),
// counts how many TextureEffect leaves declare a Subset uniform, and records the global index of the
// coverage tiled texture's transform when isCoverage is set.
static void WalkCoverageStructure(const FragmentProcessor* root, const char* label,
                                  int* coordCursor, int* subsetCount, bool isCoverage,
                                  int* tiledCoordIndex, int* textureEffectCount) {
  FragmentProcessor::Iter iter(root);
  const FragmentProcessor* fp = iter.next();
  while (fp != nullptr) {
    LOGI("[L2COVFP] %s fp=%s coordTransforms=%zu samplers=%zu firstCoordIndex=%d", label,
         fp->name().c_str(), fp->numCoordTransforms(), fp->numTextureSamplers(), *coordCursor);
    if (fp->name() == "TextureEffect") {
      (*textureEffectCount)++;
      auto* te = static_cast<const TextureEffect*>(fp);
      if (te->hasSubset()) {
        (*subsetCount)++;
      }
      if (isCoverage && fp->numCoordTransforms() > 0 && *tiledCoordIndex < 0) {
        *tiledCoordIndex = *coordCursor;
      }
    }
    *coordCursor += static_cast<int>(fp->numCoordTransforms());
    fp = iter.next();
  }
}

// Structural verification for the "coverage local-texture-mask" (type=3) feasibility question. The
// remaining coverage misses are color=TextureEffect with coverage=Xfermode-dst(TiledTextureEffect).
// Serving them through a shared QuadTextureFill-style uniform block requires the coverage texture's
// uniforms to coexist with the color texture's. This test builds the EXACT color + coverage FP
// forest the render path produces and inspects it to answer two questions with measured data rather
// than assumption:
//   (1) coordTransform indexing: the GP assigns indices per transform in FP-tree order across the
//       whole forest, so the color texture's transform is index 0 and the coverage texture's is a
//       later distinct index -> distinct uniform names (CoordTransformMatrix_0 / _N). Coordinate
//       binding is therefore NOT the blocker.
//   (2) Subset uniform: GLSLTextureEffect emits a base-named "Subset" uniform when hasSubset() is
//       true, and the precompiled binding mode strips the per-processor suffix (skipSuffix), so two
//       Subset-carrying textures would write the same slot -> collision. The test reports how many
//       of the two textures declare a Subset so the collision risk is measured.
TGFX_TEST(AOTL2AuditTest, CoverageTiledMaskFPStructure) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  BlockAllocator allocator;

  // Reconstruct the exact processor forest the render path builds for the remaining coverage misses:
  //   color    = TextureEffect (the image being drawn)
  //   coverage = MulInputByChildAlpha(TextureEffect) == XfermodeFragmentProcessor-dst wrapping the
  //              mask image, which is precisely what ShaderMaskFilter::asFragmentProcessor produces.
  // The two textures are intentionally different sizes: if a shared QuadTextureFill-style uniform
  // block declared a single "Subset" slot, both TextureEffects would write it (GLSLTextureEffect
  // sets Subset whenever the field exists, using full texture bounds), and the two differing bounds
  // would alias under skipSuffix. Distinct sizes make that aliasing concrete.
  auto colorProxy =
      context->proxyProvider()->createTextureProxy({}, 16, 16, PixelFormat::RGBA_8888);
  auto maskProxy = context->proxyProvider()->createTextureProxy({}, 8, 8, PixelFormat::ALPHA_8);
  ASSERT_TRUE(colorProxy != nullptr && colorProxy->getTextureView() != nullptr);
  ASSERT_TRUE(maskProxy != nullptr && maskProxy->getTextureView() != nullptr);

  SamplingArgs colorSampling(TileMode::Clamp, TileMode::Clamp, {}, SrcRectConstraint::Fast);
  SamplingArgs maskSampling(TileMode::Decal, TileMode::Decal, {}, SrcRectConstraint::Fast);
  auto colorFP = TextureEffect::Make(&allocator, colorProxy, colorSampling);
  auto maskTE = TextureEffect::Make(&allocator, maskProxy, maskSampling);
  ASSERT_TRUE(colorFP != nullptr && maskTE != nullptr);
  auto coverageFP = FragmentProcessor::MulInputByChildAlpha(&allocator, std::move(maskTE), false);
  ASSERT_TRUE(coverageFP != nullptr);

  int coordCursor = 0;
  int colorSubsetCount = 0;
  int coverageSubsetCount = 0;
  int coverageTiledCoordIndex = -1;
  int colorTextureCount = 0;
  int coverageTextureCount = 0;
  WalkCoverageStructure(colorFP.get(), "color", &coordCursor, &colorSubsetCount, false,
                        &coverageTiledCoordIndex, &colorTextureCount);
  WalkCoverageStructure(coverageFP.get(), "coverage", &coordCursor, &coverageSubsetCount, true,
                        &coverageTiledCoordIndex, &coverageTextureCount);

  LOGI(
      "[L2COVFP] backend=%s coverageRoot=%s totalCoordTransforms=%d "
      "colorSubset=%d coverageSubset=%d coverageTiledCoordIndex=%d",
      TGFX_BACKEND_NAME, coverageFP->name().c_str(), coordCursor, colorSubsetCount,
      coverageSubsetCount, coverageTiledCoordIndex);

  // (1) The coverage FP is the Xfermode-dst wrapper documented in CoverageTiledMaskCurrentlyMisses.
  EXPECT_NE(coverageFP->name().find("Xfermode"), std::string::npos) << coverageFP->name();
  // (2) Coordinate binding is collision-free: the coverage texture's transform lands at a global
  // index strictly after the color texture's (index 0), so they map to distinct CoordTransformMatrix
  // uniforms. A value > 0 proves the coverage transform is separately addressable — coordinate
  // binding is NOT the type=3 blocker.
  EXPECT_GT(coverageTiledCoordIndex, 0)
      << "coverage tiled texture transform must be a distinct (non-zero) global coord index";
  // Exactly one textured leaf on each side (the color image and the mask image), each with its own
  // sampler. Both samplers bind by traversal order, so they occupy distinct binding points.
  EXPECT_EQ(colorTextureCount, 1);
  EXPECT_EQ(coverageTextureCount, 1);
}

// Renders a multi-stop linear gradient into an anti-aliased, pixel-unaligned rect. The AA edges make
// the QuadPerEdgeAA GP emit a per-vertex coverage attribute, and a >2-stop gradient uses the
// Dual/UnrolledBinary colorizer — the combination that previously missed (the gradient kernels
// rejected coverage-carrying draws) and is now served via the HAS_VCOVERAGE dimension.
static void RenderGradientCoverageScene(Context* context, int width, int height,
                                        Bitmap* outBitmap) {
  context->globalCache()->clearPrograms();
  auto surface = Surface::Make(context, width, height);
  ASSERT_TRUE(surface != nullptr);
  Paint paint = {};
  paint.setAntiAlias(true);
  std::vector<Color> colors = {Color::Red(), Color::Green(), Color::Blue(), Color::White()};
  paint.setShader(Shader::MakeLinearGradient({0, 0}, {static_cast<float>(width), 0}, colors, {}));
  surface->getCanvas()->drawRect(Rect::MakeLTRB(10.5f, 10.5f, static_cast<float>(width) - 10.5f,
                                                static_cast<float>(height) - 10.5f),
                                 paint);
  context->flushAndSubmit(true);
  ASSERT_TRUE(outBitmap->allocPixels(width, height));
  auto pixels = outBitmap->lockPixels();
  ASSERT_TRUE(surface->readPixels(outBitmap->info(), pixels));
  outBitmap->unlockPixels();
}

// Validates the gradient HAS_VCOVERAGE fix: an anti-aliased multi-stop gradient draw must now hit a
// precompiled artifact (zero NoMatchingRule) and render identically to the JIT path, proving the
// per-vertex coverage varying is composited correctly in the Dual/UnrolledBinary gradient kernels.
TGFX_TEST(AOTL2AuditTest, GradientCoverageMatchesJIT) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto* cache = context->precompiledShaderCache();

  int width = 160;
  int height = 160;

  cache->unload();
  cache->resetStats();
  context->globalCache()->resetProgramStats();
  Bitmap referenceBitmap = {};
  {
    ScopedAOTStatsPause statsPause(context, true);
    RenderGradientCoverageScene(context, width, height, &referenceBitmap);
  }

  ASSERT_TRUE(cache->loadBundle(ProjectPath::Absolute(AuditBundlePath())));
  cache->resetStats();
  context->globalCache()->resetProgramStats();
  Bitmap candidateBitmap = {};
  RenderGradientCoverageScene(context, width, height, &candidateBitmap);
  uint32_t noMatch = cache->fallbackCount(PrecompiledFallbackReason::NoMatchingRule);
  uint32_t hits = cache->hitCount();
  cache->unload();

  Pixmap referencePixmap(referenceBitmap);
  Pixmap candidatePixmap(candidateBitmap);
  AOTToleranceSpec spec = {};
  spec.maxChannelDiff = 1;
  spec.maxDiffPixelRatio = 1.0;
  spec.structuralChannelDiff = 2;
  auto result = AOTToleranceCompare::Compare(referencePixmap, candidatePixmap, spec);
  LOGI("[L2GRAD] backend=%s shape=GradientCoverage noMatch=%u hits=%u maxDelta=%d pass=%d",
       TGFX_BACKEND_NAME, noMatch, hits, result.maxChannelDiff, result.passed ? 1 : 0);

  if (IsOpenGLAuditBackend()) {
    EXPECT_GT(noMatch, 0u);
    EXPECT_EQ(hits, 0u);
  } else {
    EXPECT_EQ(noMatch, 0u) << "anti-aliased multi-stop gradient still misses the precompiled cache";
    EXPECT_GT(hits, 0u) << "expected a precompiled artifact hit for the AA gradient draw";
  }
  EXPECT_FALSE(result.structuralDifference);
  EXPECT_TRUE(result.passed) << "AOT gradient-with-coverage diverges from JIT, maxDelta="
                             << result.maxChannelDiff;
}

}  // namespace tgfx

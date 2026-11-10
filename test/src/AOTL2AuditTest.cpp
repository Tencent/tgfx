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
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

// L2 cross-validation audit.
//
// For every shape the L2 decomposition route can currently serve, render the SAME scene twice:
//   reference = decomposition disabled (plain precompiled matcher route),
//   candidate = decomposition enabled  (L2 path).
// The two must agree within <= 1 LSB, because a single-pass fused shape differs only by the
// route that selected its precompiled variant. Any per-channel difference above 1 is a
// mapping/uniform-recipe bug, not a tolerance matter, and fails hard.
//
// Each case also emits one machine-readable matrix row:
//   [L2AUDIT] backend=<b> shape=<name> served=<0/1> refHits=<n> candHits=<n> maxDelta=<d> pass=<0/1>
// so a three-backend run yields the shape x backend validation matrix from the design.

#include <string>
#include "base/TGFXTest.h"
#include "core/utils/Log.h"
#include "gpu/ContextInternal.h"
#include "gpu/GlobalCache.h"
#include "gpu/aot/bundle/PrecompiledShaderCache.h"
#include "gtest/gtest.h"
#include "tgfx/core/Bitmap.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/ColorFilter.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Pixmap.h"
#include "tgfx/core/Shader.h"
#include "tgfx/core/Surface.h"
#include "tgfx/gpu/Context.h"
#include "utils/AOTToleranceCompare.h"
#include "utils/TestUtils.h"

namespace tgfx {

#ifndef TGFX_BACKEND_NAME
#define TGFX_BACKEND_NAME "opengl"
#endif

// Renders one scene and reports its pixels + the AOT artifact hit count. The scene is built by the
// caller-supplied paint so each audit case exercises a different L2-serviceable shape.
static void RenderScene(Context* context, PrecompiledShaderCache* cache,
                        const std::shared_ptr<Image>& image, const Paint& paint, int width,
                        int height, bool decompositionEnabled, Bitmap* outBitmap,
                        uint32_t* outHits) {
  cache->setDecompositionEnabled(decompositionEnabled);
  // The reference leg forces the pre-decomposition route on purpose, so a shape only the chain
  // serves (the color-matrix image, now that no dedicated family claims it) has no program there.
  // That drop is this fixture's own doing, not a coverage gap of the production route.
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

// Audits the shapes the L2 route serves today: a plain textured image draw, validated to be
// pixel-equivalent (<= 1 LSB) between the pre-decomposition and the L2 paths. The color-matrix
// image left the audit with its dedicated family: no family serves it with decomposition off any
// more, so the reference leg would compare two blank renders.
TGFX_TEST(AOTL2AuditTest, ServedShapesMatchPlainPathWithinOneLSB) {
  auto image = MakeImage("resources/apitest/test_timestretch.png");
  ASSERT_TRUE(image != nullptr);
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto* cache = ContextCacheAccess::Cache(context);
  ASSERT_TRUE(LoadEmbeddedBundle(cache, context->backend()));

  {
    Paint paint = {};
    AuditCase(context, cache, image, paint, "plain-image");
  }

  cache->setDecompositionEnabled(true);  // restore production default (gate on)
}

// Renders a full-surface rect painted with the given shader. Used to exercise blend shaders (the
// drawImage helper cannot carry a paint shader).
static void RenderShaderScene(Context* context, PrecompiledShaderCache* cache,
                              const std::shared_ptr<Shader>& shader, int width, int height,
                              bool decompositionEnabled, Bitmap* outBitmap, uint32_t* outHits,
                              uint32_t* outNoMatch) {
  cache->setDecompositionEnabled(decompositionEnabled);
  // The reference leg forces the pre-decomposition route on purpose, so a shape only the chain
  // serves (the color-matrix image, now that no dedicated family claims it) has no program there.
  // That drop is this fixture's own doing, not a coverage gap of the production route.
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

TGFX_TEST(AOTL2AuditTest, CleanBlendPrefersPointwiseChain) {
  auto imageA = MakeImage("resources/apitest/test_timestretch.png");
  auto imageB = MakeImage("resources/apitest/mandrill_128.png");
  ASSERT_TRUE(imageA != nullptr && imageB != nullptr);
  ContextScope scope;
  auto context = scope.getContext();
  SKIP_ON_SWIFTSHADER(context);
  ASSERT_TRUE(context != nullptr);
  auto* cache = ContextCacheAccess::Cache(context);
  ASSERT_TRUE(LoadEmbeddedBundle(cache, context->backend()));
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
    chainHit =
        chainHit || record.shaderName == "PointwiseChainShader" || record.shaderName == "Chain";
  }
  EXPECT_TRUE(chainHit);
}

}  // namespace tgfx

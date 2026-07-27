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
#include "tgfx/core/Paint.h"
#include "tgfx/core/Pixmap.h"
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

  cache->setDecompositionEnabled(false);
  cache->unload();
}

}  // namespace tgfx

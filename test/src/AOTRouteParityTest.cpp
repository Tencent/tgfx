/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include <set>
#include <string>
#include "gpu/ContextInternal.h"
#include "gpu/GlobalCache.h"
#include "gpu/aot/bundle/PrecompiledShaderCache.h"
#include "gtest/gtest.h"
#include "tgfx/core/Bitmap.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Surface.h"
#include "utils/AOTToleranceCompare.h"
#include "utils/ContextScope.h"
#include "utils/TestUtils.h"

namespace tgfx {
namespace {

// A sub-pixel diagonal stroke: the compositor routes it to the two hairline ops (a line and a
// quad), and the 4x target is what makes the analytic-edge switch observable — every draw on such
// a target carries AAType::MSAA.
void DrawHairlineScene(Canvas* canvas) {
  canvas->clear(Color::Transparent());
  Paint paint = {};
  paint.setColor(Color::FromRGBA(24, 96, 216, 200));
  paint.setAntiAlias(true);
  paint.setStyle(PaintStyle::Stroke);
  paint.setStrokeWidth(0.5f);
  canvas->drawLine(12.5f, 12.25f, 51.5f, 47.75f, paint);
}

Bitmap RenderScene(Context* context) {
  Bitmap bitmap = {};
  auto surface = Surface::Make(context, 64, 64, /*alphaOnly=*/false, /*sampleCount=*/4);
  if (surface == nullptr) {
    return bitmap;
  }
  DrawHairlineScene(surface->getCanvas());
  context->flushAndSubmit(true);
  if (!bitmap.allocPixels(64, 64)) {
    return Bitmap();
  }
  auto pixels = bitmap.lockPixels();
  if (pixels == nullptr || !surface->readPixels(bitmap.info(), pixels)) {
    bitmap.unlockPixels();
    return Bitmap();
  }
  bitmap.unlockPixels();
  return bitmap;
}

std::set<std::string> ServedShaders(PrecompiledShaderCache* cache) {
  std::set<std::string> names = {};
  for (const auto& record : cache->hitRecords()) {
    names.insert(record.shaderName);
  }
  return names;
}

// The two routes must agree on an MSAA target: the dedicated hairline family runs the analytic
// edge, so the interpreter chain has to run it too. A disagreement is a hard 0-vs-1 flip of the
// AAEnabled uniform and shows up as a large per-channel difference at the stroke's edge pixels.
const AOTToleranceSpec HairlineRouteTolerance() {
  AOTToleranceSpec spec = {};
  spec.maxChannelDiff = 2;
  spec.maxDiffPixelRatio = 1.0;
  spec.significantChannelDiff = 0;
  spec.structuralChannelDiff = 16;
  return spec;
}

}  // namespace

TGFX_TEST(AOTRouteParityTest, HairlineMatchesBothRoutesOnMSAA) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto* cache = ContextCacheAccess::Cache(context);
  ASSERT_NE(cache, nullptr);
  if (!cache->isLoaded()) {
    GTEST_SKIP() << "This build embeds no bundle for the backend";
  }
  cache->setDiagnosticRecordingEnabled(true);

  cache->resetStats();
  cache->setDedicatedKernelsEnabled(true);
  auto dedicated = RenderScene(context);
  auto dedicatedShaders = ServedShaders(cache);
  if (dedicated.isEmpty() || dedicatedShaders.empty()) {
    cache->setDedicatedKernelsEnabled(true);
    cache->setDiagnosticRecordingEnabled(false);
    GTEST_SKIP() << "the backend does not serve this hairline draw";
  }

  cache->resetStats();
  cache->setDedicatedKernelsEnabled(false);
  context->globalCache()->clearPrograms();
  auto interpreted = RenderScene(context);
  auto interpretedShaders = ServedShaders(cache);

  cache->setDedicatedKernelsEnabled(true);
  cache->setDiagnosticRecordingEnabled(false);

  ASSERT_FALSE(interpreted.isEmpty()) << "the interpreter route dropped the hairline draw";
  // The comparison is only meaningful when the two renders were actually served by different
  // families; two runs of one family would hide exactly the drift this test exists to catch.
  EXPECT_EQ(dedicatedShaders.count("Chain"), 0u)
      << "the dedicated route served the draw through the interpreter";
  EXPECT_EQ(interpretedShaders.size(), 1u);
  EXPECT_EQ(interpretedShaders.count("Chain"), 1u) << "the interpreter route served another family";

  Pixmap dedicatedPixmap(dedicated);
  Pixmap interpretedPixmap(interpreted);
  auto result =
      AOTToleranceCompare::Compare(dedicatedPixmap, interpretedPixmap, HairlineRouteTolerance());
  EXPECT_TRUE(result.passed) << "the two hairline routes disagree: maxChannelDiff="
                             << result.maxChannelDiff << " diffPixels=" << result.diffPixelCount
                             << "/" << result.totalPixelCount;
}

}  // namespace tgfx

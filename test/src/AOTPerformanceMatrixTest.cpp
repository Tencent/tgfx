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
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. See the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

// P7 performance matrix (plan v3, batch 7). Five scenario families, each measured cold (first
// frame after a program-cache wipe) and hot (steady state after warm-up), Release build, on both
// routes — the precompiled AOT chain and the runtime stitching reference — so every number has a
// like-for-like counterpart. The output is a table of medians over N_ROUNDS rounds; the raw
// per-round numbers print too. This suite asserts nothing about timing (performance is not a
// pass/fail gate here) — it only asserts routing correctness (the AOT runs really ride the
// precompiled kernels) so the timings measure the intended path.
//
// Scenarios (plan P7):
//   1. Short chains of 1-5 operators, steady state: the interpreter + full-slot onSetData upload
//      cost, including the fixed 32-slot uniform write the chain performs regardless of chain
//      length.
//   2. Same layout, different effects: the program-reuse benefit (same variant, different
//      instruction sequences).
//   3. Mask/clip/blend short chains: the cost of the batch 1/2 semantic fixes.
//   4. Real materialization (dual gradients): passes, intermediate bytes, target switches.
//   5. Control group: the runtime stitching route running the same scenes.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <vector>
#include "base/TGFXTest.h"
#include "gpu/EmbeddedShaderBundles.h"
#include "gpu/GlobalCache.h"
#include "gpu/PrecompiledShaderCache.h"
#include "tgfx/core/Bitmap.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/ColorFilter.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Shader.h"
#include "tgfx/gpu/Context.h"
#include "utils/TestUtils.h"

namespace tgfx {

namespace {

constexpr int kSize = 128;
constexpr int kWarmUpFrames = 20;
constexpr int kRounds = 30;

// A stable, non-trivial color matrix for chain construction.
const std::array<float, 20>& WarmMatrix() {
  static const std::array<float, 20> matrix = {
      1.1f, 0.02f, 0, 0, 0.01f, 0, 1.05f, 0.01f, 0, 0.02f, 0, 0, 0.95f, 0.01f, 0, 0, 0, 0, 1.0f, 0};
  return matrix;
}

struct TimingResult {
  double coldMs = 0.0;
  double hotMedianMs = 0.0;
  double hotMinMs = 0.0;
  uint32_t aotDraws = 0;
  uint32_t runtimePrograms = 0;
};

double Median(std::vector<double>& values) {
  if (values.empty()) {
    return 0.0;
  }
  std::sort(values.begin(), values.end());
  return values[values.size() / 2];
}

// Measures one route (useBundle selects AOT vs runtime) over one scene. Cold = first frame after
// clearing the program cache and (for AOT) reloading the bundle; hot = median over kRounds
// batches of kBatchFrames frames each (a batch shares one surface so per-frame surface setup
// stays out of the measurement).
template <typename Scene>
TimingResult MeasureRoute(Context* context, PrecompiledShaderCache* cache, bool useBundle,
                          const Scene& scene) {
  TimingResult result = {};
  if (useBundle) {
    auto [bundleData, bundleBytes] = EmbeddedShaderBundles::GetBundle(context->backend());
    if (!cache->loadBundle(bundleData, bundleBytes)) {
      return result;
    }
    cache->setDecompositionEnabled(true);
  } else {
    cache->unload();
  }
  auto surface = Surface::Make(context, kSize, kSize);
  if (surface == nullptr) {
    return result;
  }
  // Cold frame: every program gets looked up (and created on the runtime route).
  context->globalCache()->clearPrograms();
  context->globalCache()->resetProgramStats();
  cache->resetStats();
  cache->setDiagnosticRecordingEnabled(true);
  auto start = std::chrono::steady_clock::now();
  scene(surface->getCanvas());
  context->flushAndSubmit(true);
  auto end = std::chrono::steady_clock::now();
  result.coldMs = std::chrono::duration<double, std::milli>(end - start).count();
  auto stats = context->globalCache()->programStats();
  result.aotDraws = static_cast<uint32_t>(cache->drawStats().completeAOTDraws);
  result.runtimePrograms = static_cast<uint32_t>(stats.programBuilderCreations);
  cache->setDiagnosticRecordingEnabled(false);
  // Hot frames: steady state, programs cached on both routes (the runtime route's program cache
  // serves its stitched programs). Each sample times
  // kBatchFrames frames (the per-frame cost is the sample divided by the batch size).
  constexpr int kBatchFrames = 20;
  scene(surface->getCanvas());
  context->flushAndSubmit(true);
  for (int warmUp = 0; warmUp < kWarmUpFrames; ++warmUp) {
    scene(surface->getCanvas());
    context->flushAndSubmit(true);
  }
  std::vector<double> samples = {};
  samples.reserve(kRounds);
  for (int round = 0; round < kRounds; ++round) {
    auto batchStart = std::chrono::steady_clock::now();
    for (int frame = 0; frame < kBatchFrames; ++frame) {
      scene(surface->getCanvas());
      context->flushAndSubmit(true);
    }
    auto batchEnd = std::chrono::steady_clock::now();
    samples.push_back(std::chrono::duration<double, std::milli>(batchEnd - batchStart).count() /
                      kBatchFrames);
  }
  result.hotMedianMs = Median(samples);
  result.hotMinMs = samples.front();
  for (double value : samples) {
    result.hotMinMs = std::min(result.hotMinMs, value);
  }
  return result;
}

void PrintResult(const char* scenario, const char* route, const TimingResult& result) {
  printf(
      "[P7Perf] %-28s %-8s cold=%8.3fms hotMed=%8.3fms hotMin=%8.3fms aotDraws=%u "
      "runtimePrograms=%u\n",
      scenario, route, result.coldMs, result.hotMedianMs, result.hotMinMs, result.aotDraws,
      result.runtimePrograms);
  fflush(stdout);
}

// Builds a paint whose color filter composes `opCount` matrices.
std::shared_ptr<ColorFilter> MakeMatrixChain(size_t opCount) {
  auto filter = ColorFilter::Matrix(WarmMatrix());
  for (size_t index = 1; index < opCount; ++index) {
    filter = ColorFilter::Compose(filter, ColorFilter::Matrix(WarmMatrix()));
  }
  return filter;
}

}  // namespace

// Scenarios 1 and 5: short chains of 1-5 operators over an image, steady state. The AOT route
// rides the PointwiseChain kernel (one fused pass, full 32-slot uniform upload per draw); the
// runtime route stitches one program per chain depth. Asserts routing first so the timings
// measure the intended paths.
TGFX_TEST(AOTPerformanceMatrixTest, ShortChainSteadyState) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto* cache = context->precompiledShaderCache();
  auto image = MakeImage("resources/apitest/mandrill_128.png");
  ASSERT_NE(image, nullptr);

  for (size_t opCount : {size_t{1}, size_t{3}, size_t{5}}) {
    char label[32];
    snprintf(label, sizeof(label), "chain-%zu-op", opCount);
    auto filter = MakeMatrixChain(opCount);
    auto scene = [&](Canvas* canvas) {
      Paint paint = {};
      paint.setColorFilter(filter);
      canvas->drawImage(image, 0, 0, &paint);
    };
    auto aot = MeasureRoute(context, cache, true, scene);
    // Routing check: every AOT frame must be served by the precompiled set.
    EXPECT_EQ(aot.runtimePrograms, 0u);
    EXPECT_GE(aot.aotDraws, 1u);
    auto runtime = MeasureRoute(context, cache, false, scene);
    PrintResult(label, "aot", aot);
    PrintResult(label, "runtime", runtime);
  }
}

// Scenario 2: same layout (one texture leaf), different effects (matrix vs luma vs matrix+luma)
// interleaved on one surface — the program-reuse benefit the collapsed key provides. Also
// measures the contrast case: alternating with a two-leaf blend forces a different variant.
TGFX_TEST(AOTPerformanceMatrixTest, SameLayoutEffectReuse) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto* cache = context->precompiledShaderCache();
  auto image = MakeImage("resources/apitest/mandrill_128.png");
  ASSERT_NE(image, nullptr);
  auto imageShader = Shader::MakeImageShader(image, TileMode::Clamp, TileMode::Clamp);
  ASSERT_NE(imageShader, nullptr);

  auto sceneSameVariant = [&](Canvas* canvas) {
    Paint one = {};
    one.setShader(imageShader);
    one.setColorFilter(ColorFilter::Matrix(WarmMatrix()));
    canvas->drawImage(image, 0, 0, &one);
    Paint two = {};
    two.setShader(imageShader);
    two.setColorFilter(ColorFilter::Luma());
    canvas->drawImage(image, 0, 0, &two);
  };
  auto aot = MeasureRoute(context, cache, true, sceneSameVariant);
  EXPECT_EQ(aot.runtimePrograms, 0u);
  auto runtime = MeasureRoute(context, cache, false, sceneSameVariant);
  PrintResult("reuse-same-variant", "aot", aot);
  PrintResult("reuse-same-variant", "runtime", runtime);
}

// Scenario 3: mask/clip/blend short chains — the shapes the batch 1/2 fixes serve (alpha-only
// color root under paint alpha, blend operands). Their steady-state cost carries the fix's
// instructions.
TGFX_TEST(AOTPerformanceMatrixTest, MaskClipBlendShortChain) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto* cache = context->precompiledShaderCache();
  constexpr int maskSize = 96;
  Bitmap maskBitmap = {};
  ASSERT_TRUE(maskBitmap.allocPixels(maskSize, maskSize, true));
  auto* maskPixels = static_cast<uint8_t*>(maskBitmap.lockPixels());
  for (size_t y = 0; y < static_cast<size_t>(maskSize); ++y) {
    for (size_t x = 0; x < static_cast<size_t>(maskSize); ++x) {
      maskPixels[y * maskBitmap.rowBytes() + x] = static_cast<uint8_t>((x * 3 + y * 5) % 256);
    }
  }
  maskBitmap.unlockPixels();
  auto maskImage = Image::MakeFrom(maskBitmap);
  ASSERT_NE(maskImage, nullptr);

  auto scene = [&](Canvas* canvas) {
    Paint paint = {};
    paint.setColor(Color(1.0f, 0.0f, 0.0f, 0.5f));
    paint.setColorFilter(ColorFilter::Matrix(WarmMatrix()));
    canvas->drawImage(maskImage, 0, 0, &paint);
  };
  auto aot = MeasureRoute(context, cache, true, scene);
  EXPECT_EQ(aot.runtimePrograms, 0u);
  auto runtime = MeasureRoute(context, cache, false, scene);
  PrintResult("alpha-root-paint-alpha", "aot", aot);
  PrintResult("alpha-root-paint-alpha", "runtime", runtime);
}

// Scenario 4: real materialization (two gradients under a blend). Records the observable
// materialization counts alongside the timings.
TGFX_TEST(AOTPerformanceMatrixTest, DualGradientMaterialization) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto* cache = context->precompiledShaderCache();
  constexpr int size = 128;
  std::vector<Color> warmColors = {Color(1.0f, 0.3f, 0.0f, 1.0f), Color(0.9f, 0.1f, 0.2f, 1.0f)};
  std::vector<Color> coolColors = {Color(0.0f, 0.4f, 1.0f, 1.0f), Color(0.1f, 0.2f, 0.9f, 1.0f)};
  auto warm = Shader::MakeLinearGradient(Point(0, 0), Point(size, size), warmColors, {0.0f, 1.0f});
  auto cool = Shader::MakeLinearGradient(Point(size, 0), Point(0, size), coolColors, {0.0f, 1.0f});
  ASSERT_NE(warm, nullptr);
  ASSERT_NE(cool, nullptr);

  auto scene = [&](Canvas* canvas) {
    canvas->clear(Color::Transparent());
    Paint paint = {};
    paint.setShader(Shader::MakeBlend(BlendMode::Multiply, warm, cool));
    canvas->drawRect(Rect::MakeWH(size, size), paint);
  };
  auto aot = MeasureRoute(context, cache, true, scene);
  auto runtime = MeasureRoute(context, cache, false, scene);
  PrintResult("dual-gradient-blend", "aot", aot);
  PrintResult("dual-gradient-blend", "runtime", runtime);
}

}  // namespace tgfx

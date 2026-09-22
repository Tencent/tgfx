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

// Performance matrix:  Five scenario families, each measured cold (first
// frame after a program-cache wipe) and hot (steady state after warm-up) on both routes — the
// precompiled AOT chain and the runtime stitching reference — so every number has a
// like-for-like counterpart. The output is a table of medians over N_ROUNDS rounds; the raw
// per-round numbers print too. The suite first asserts routing correctness (the AOT runs really
// ride the precompiled kernels) so the timings measure the intended path, then applies the
// calibrated ratio gates in AssertPerformanceGates (see its comment for scope and limits —
// the budgets bound further degradation; they do not approve the current absolute cost).
//
// Scenarios (plan P7):
//   1. Short chains of 1-5 operators, steady state: the interpreter + full-slot onSetData upload
//      cost, including the fixed 32-slot uniform write the chain performs regardless of chain
//      length.
//   2. Same layout, different effects: the program-reuse benefit (same variant, different
//      instruction sequences).
//   3. Mask/clip/blend short chains: the cost of the coverage/XP semantic fixes.
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
// Cold samples per route (program-cold: the program cache is cleared and the bundle reloaded
// before each sample; process-cold is a property of how the test binary is launched, not
// repeatable in-process, and is documented as such). 3 samples give a min/median spread for
// the single-sample cold numbers the audit flagged.
constexpr int kColdSamples = 3;

// A stable, non-trivial color matrix for chain construction.
const std::array<float, 20>& WarmMatrix() {
  static const std::array<float, 20> matrix = {
      1.1f, 0.02f, 0, 0, 0.01f, 0, 1.05f, 0.01f, 0, 0.02f, 0, 0, 0.95f, 0.01f, 0, 0, 0, 0, 1.0f, 0};
  return matrix;
}

struct TimingResult {
  std::vector<double> coldSamples = {};
  double coldMedianMs = 0.0;
  std::vector<double> hotSamples = {};
  double hotMedianMs = 0.0;
  double hotMinMs = 0.0;
  double hotP95Ms = 0.0;
  uint32_t aotDraws = 0;
  uint32_t runtimePrograms = 0;
  // False when the measurement itself failed (bundle load or surface creation): a failed
  // measurement must fail the gate, never pass it with zero timings.
  bool measurementValid = false;
};

double Median(std::vector<double>& values) {
  if (values.empty()) {
    return 0.0;
  }
  std::sort(values.begin(), values.end());
  return values[values.size() / 2];
}

double Percentile(std::vector<double> values, double percentile) {
  if (values.empty()) {
    return 0.0;
  }
  std::sort(values.begin(), values.end());
  auto index = static_cast<size_t>(percentile * static_cast<double>(values.size() - 1));
  return values[index];
}

// Measures one route (useBundle selects AOT vs runtime) over one scene. Cold = kColdSamples
// first frames after clearing the program cache and (for AOT) reloading the bundle — the
// in-process, program-level cold (process cold is a launch property, not measurable here).
// Hot = median/p95 over kRounds batches of kBatchFrames frames each (a batch shares one
// surface so per-frame surface setup stays out of the measurement). `size` parameterizes the
// surface so the fill-rate axis (128 vs 512) is covered by the representative chain scenario.
// P9 ratio gates. Calibrated 2026-09-22, Apple M4 Pro, Debug build, 10 full-matrix runs per
// backend (medians/maxima in the manifest) — the budgets are set above the observed maxima so
// the gate catches FURTHER degradation; they are not an approval of the current absolute cost,
// and they are not validated on other machines or build types (re-calibrate before relying on
// them elsewhere):
//   Metal cold aot/runtime: med 0.03-0.06, max 0.08 → budget 0.15. The bundle carries real
//     metallib binaries, so pipeline creation from them stays far cheaper than compiling the
//     JIT tree's MSL; this protects the AOT route's main cold-start advantage.
//   GL cold aot/runtime:    med 5.7-12.6, max 14.6  → budget 20. The GL bundle stores SOURCE,
//     so first use compiles the (large) chain kernel while the JIT route compiles a small
//     specialized tree; currently slower on this axis — the budget only bounds further cold
//     regressions (bundle/reflection bloat, slower program assembly).
//   Both hot aot/runtime:   med 1.5-2.7,  max 3.63  → budget 4.0. The chain kernel interprets a
//     uniform-driven slot program while the JIT route runs a specialized tree; the measured
//     steady-state ratio lands here. The budget bounds growth of that gap — it does not assert
//     the current gap is acceptable.
// A failed or empty measurement fails the gate instead of silently passing with zero ratios.
void AssertPerformanceGates(Context* context, const std::string& label, const TimingResult& aot,
                            const TimingResult& runtime) {
  EXPECT_TRUE(aot.measurementValid) << label << " aot measurement failed (bundle/surface)";
  EXPECT_TRUE(runtime.measurementValid) << label << " runtime measurement failed (surface)";
  if (!aot.measurementValid || !runtime.measurementValid) {
    return;
  }
  EXPECT_GT(aot.coldMedianMs, 0.0) << label << " aot cold timing is zero";
  EXPECT_GT(aot.hotMedianMs, 0.0) << label << " aot hot timing is zero";
  EXPECT_GT(runtime.coldMedianMs, 0.0) << label << " runtime cold timing is zero";
  EXPECT_GT(runtime.hotMedianMs, 0.0) << label << " runtime hot timing is zero";
  if (runtime.coldMedianMs <= 0.0 || runtime.hotMedianMs <= 0.0 || aot.coldMedianMs <= 0.0 ||
      aot.hotMedianMs <= 0.0) {
    return;
  }
  const double coldBudget = context->backend() == Backend::Metal ? 0.15 : 20.0;
  EXPECT_LE(aot.coldMedianMs / runtime.coldMedianMs, coldBudget)
      << label << " cold ratio (aot/runtime)";
  EXPECT_LE(aot.hotMedianMs / runtime.hotMedianMs, 4.0) << label << " hot ratio (aot/runtime)";
}

template <typename Scene>
TimingResult MeasureRoute(Context* context, PrecompiledShaderCache* cache, bool useBundle,
                          const Scene& scene, int size = kSize) {
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
  auto surface = Surface::Make(context, size, size);
  if (surface == nullptr) {
    return result;
  }
  // Cold frames: every program gets looked up (and created on the runtime route). Each sample
  // clears the program cache independently; the samples' spread (min..max) shows the stability
  // the audit's single-cold-sample design could not.
  for (int cold = 0; cold < kColdSamples; ++cold) {
    context->globalCache()->clearPrograms();
    context->globalCache()->resetProgramStats();
    cache->resetStats();
    cache->setDiagnosticRecordingEnabled(true);
    auto start = std::chrono::steady_clock::now();
    scene(surface->getCanvas());
    context->flushAndSubmit(true);
    auto end = std::chrono::steady_clock::now();
    result.coldSamples.push_back(std::chrono::duration<double, std::milli>(end - start).count());
  }
  result.coldMedianMs = Median(result.coldSamples);
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
  result.hotP95Ms = Percentile(samples, 0.95);
  result.hotSamples = std::move(samples);
  result.measurementValid = true;
  return result;
}

void PrintResult(const char* scenario, const char* route, const TimingResult& result,
                 int size = kSize) {
#ifdef NDEBUG
  constexpr const char* kBuildType = "release";
#else
  constexpr const char* kBuildType = "debug";
#endif
  printf(
      "[P7Perf] %-28s %-8s %7s %4d coldMed=%8.3fms hotMed=%8.3fms hotMin=%8.3fms "
      "hotP95=%8.3fms aotDraws=%u runtimePrograms=%u\n",
      scenario, route, kBuildType, size, result.coldMedianMs, result.hotMedianMs, result.hotMinMs,
      result.hotP95Ms, result.aotDraws, result.runtimePrograms);
  // Raw per-round samples for offline aggregation: the cold spread and every hot round, so
  // dispersion and outliers survive the console capture (audit rule: raw data lands on disk).
  printf("[P7PerfRaw] %s %s size=%d cold=[", scenario, route, size);
  for (size_t index = 0; index < result.coldSamples.size(); ++index) {
    printf("%s%.3f", index > 0 ? "," : "", result.coldSamples[index]);
  }
  printf("] hot=[");
  for (size_t index = 0; index < result.hotSamples.size(); ++index) {
    printf("%s%.3f", index > 0 ? "," : "", result.hotSamples[index]);
  }
  printf("]\n");
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
  SKIP_ON_SWIFTSHADER(context);
  ASSERT_NE(context, nullptr);
  auto* cache = context->precompiledShaderCache();
  auto image = MakeImage("resources/apitest/mandrill_128.png");
  ASSERT_NE(image, nullptr);

  for (int size : {128, 512}) {
    for (size_t opCount : {size_t{1}, size_t{3}, size_t{5}}) {
      char label[32];
      snprintf(label, sizeof(label), "chain-%zu-op", opCount);
      auto filter = MakeMatrixChain(opCount);
      auto scene = [&](Canvas* canvas) {
        // Fill the whole surface: the fill-rate axis scales the fragment work with the size,
        // so the 512 rounds measure a real cost difference rather than a fixed overhead.
        canvas->save();
        canvas->scale(static_cast<float>(size) / 128.0f, static_cast<float>(size) / 128.0f);
        Paint paint = {};
        paint.setColorFilter(filter);
        canvas->drawImage(image, 0, 0, &paint);
        canvas->restore();
      };
      // Route-order symmetry (audit rule): a discarded runtime measurement first, so the
      // recorded AOT numbers do not enjoy first-touch resource states the runtime numbers
      // never see (and vice versa for the second runtime pass).
      MeasureRoute(context, cache, false, scene, size);
      auto aot = MeasureRoute(context, cache, true, scene, size);
      // Routing check: every AOT frame must be served by the precompiled set.
      EXPECT_EQ(aot.runtimePrograms, 0u);
      EXPECT_GE(aot.aotDraws, 1u);
      auto runtime = MeasureRoute(context, cache, false, scene, size);
      AssertPerformanceGates(context, label, aot, runtime);
      PrintResult(label, "aot", aot, size);
      PrintResult(label, "runtime", runtime, size);
    }
  }
}

// Scenario 2: same layout (one texture leaf), different effects (matrix vs luma vs matrix+luma)
// interleaved on one surface — the program-reuse benefit the collapsed key provides. Also
// measures the contrast case: alternating with a two-leaf blend forces a different variant.
TGFX_TEST(AOTPerformanceMatrixTest, SameLayoutEffectReuse) {
  ContextScope scope;
  auto context = scope.getContext();
  SKIP_ON_SWIFTSHADER(context);
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
  // Route-order symmetry (audit rule): a discarded runtime pass first (see ShortChainSteadyState).
  MeasureRoute(context, cache, false, sceneSameVariant);
  auto aot = MeasureRoute(context, cache, true, sceneSameVariant);
  EXPECT_EQ(aot.runtimePrograms, 0u);
  auto runtime = MeasureRoute(context, cache, false, sceneSameVariant);
  AssertPerformanceGates(context, "reuse-same-variant", aot, runtime);
  PrintResult("reuse-same-variant", "aot", aot);
  PrintResult("reuse-same-variant", "runtime", runtime);
}

// Scenario 3: mask/clip/blend short chains — the shapes the coverage/XP fixes serve (alpha-only
// color root under paint alpha, blend operands). Their steady-state cost carries the fix's
// instructions.
TGFX_TEST(AOTPerformanceMatrixTest, MaskClipBlendShortChain) {
  ContextScope scope;
  auto context = scope.getContext();
  SKIP_ON_SWIFTSHADER(context);
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
  // Route-order symmetry (audit rule): a discarded runtime pass first (see ShortChainSteadyState).
  MeasureRoute(context, cache, false, scene);
  auto aot = MeasureRoute(context, cache, true, scene);
  EXPECT_EQ(aot.runtimePrograms, 0u);
  auto runtime = MeasureRoute(context, cache, false, scene);
  AssertPerformanceGates(context, "alpha-root-paint-alpha", aot, runtime);
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
  // Route-order symmetry (audit rule): a discarded runtime pass first (see ShortChainSteadyState).
  MeasureRoute(context, cache, false, scene);
  auto aot = MeasureRoute(context, cache, true, scene);
  EXPECT_GE(aot.aotDraws, 1u);
  auto runtime = MeasureRoute(context, cache, false, scene);
  AssertPerformanceGates(context, "dual-gradient-blend", aot, runtime);
  PrintResult("dual-gradient-blend", "aot", aot);
  PrintResult("dual-gradient-blend", "runtime", runtime);
}

}  // namespace tgfx

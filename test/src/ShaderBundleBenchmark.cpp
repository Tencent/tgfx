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

#include <algorithm>
#include <cstdio>
#if defined(__APPLE__)
#include <mach/mach.h>
#endif
#include <cstdlib>
#include "gpu/GlobalCache.h"
#include "gpu/PrecompiledShaderCache.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Clock.h"
#include "tgfx/core/Font.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Shader.h"
#include "tgfx/core/Surface.h"
#include "tgfx/core/Typeface.h"
#include "utils/TestUtils.h"

namespace tgfx {

// A measurement, not a check: it prints how long a process takes to create its Context and to draw
// the first frame of a mixed scene, and how many programs came from the precompiled bundle. It is
// disabled by default. Run each mode in its own process, because only a process that has not drawn
// yet has an empty program cache:
//   ./TGFXFullTest_<Backend> --gtest_also_run_disabled_tests --gtest_filter='ShaderBundleBenchmark.*'
//   TGFX_AOT_DISABLE=1 ./TGFXFullTest_<Backend> --gtest_also_run_disabled_tests ...

static void DrawScene(Canvas* canvas, const std::shared_ptr<Image>& image,
                      const std::shared_ptr<Typeface>& typeface) {
  canvas->clear(Color::White());
  Paint paint = {};
  paint.setAntiAlias(true);

  paint.setColor(Color::Red());
  canvas->drawRect(Rect::MakeXYWH(10, 10, 120, 80), paint);
  canvas->drawRRect(RRect::MakeRectXY(Rect::MakeXYWH(140, 10, 120, 80), 16, 16), paint);

  paint.setShader(Shader::MakeLinearGradient({270, 10}, {390, 90}, {Color::Red(), Color::Blue()}));
  canvas->drawRect(Rect::MakeXYWH(270, 10, 120, 80), paint);
  paint.setShader(Shader::MakeRadialGradient(
      {460, 50}, 50, {Color::White(), Color::Green(), Color::Blue()}, {0.f, 0.5f, 1.f}));
  canvas->drawRect(Rect::MakeXYWH(400, 10, 120, 80), paint);
  paint.setShader(nullptr);

  Path star = {};
  star.moveTo(560, 10);
  star.lineTo(600, 90);
  star.lineTo(530, 40);
  star.lineTo(620, 40);
  star.lineTo(550, 90);
  star.close();
  paint.setColor(Color::Blue());
  canvas->drawPath(star, paint);
  Paint stroke = paint;
  stroke.setStyle(PaintStyle::Stroke);
  stroke.setStrokeWidth(4);
  canvas->drawPath(star, stroke);

  canvas->drawImage(image, 10, 110);
  canvas->drawImageRect(image, Rect::MakeXYWH(150, 110, 200, 200),
                        SamplingOptions(FilterMode::Linear));
  canvas->drawImageRect(image->makeMipmapped(true), Rect::MakeXYWH(360, 110, 60, 60),
                        SamplingOptions(FilterMode::Linear, MipmapMode::Linear));

  Paint filtered = {};
  filtered.setImageFilter(ImageFilter::Blur(8, 8));
  canvas->drawImage(image, 440, 110, &filtered);
  filtered.setImageFilter(ImageFilter::DropShadow(6, 6, 4, 4, Color::Black()));
  canvas->drawImage(image, 600, 110, &filtered);
  filtered.setImageFilter(ImageFilter::InnerShadow(4, 4, 3, 3, Color::Black()));
  canvas->drawImage(image, 440, 260, &filtered);

  const BlendMode modes[] = {BlendMode::Multiply, BlendMode::Screen,  BlendMode::Overlay,
                             BlendMode::Darken,   BlendMode::Lighten, BlendMode::Difference};
  float x = 10;
  for (auto mode : modes) {
    Paint blend = {};
    blend.setColor(Color::Blue());
    blend.setBlendMode(mode);
    canvas->drawRect(Rect::MakeXYWH(x, 330.f, 60.f, 60.f), blend);
    x += 70;
  }

  if (typeface != nullptr) {
    Font font(typeface, 28);
    Paint text = {};
    text.setColor(Color::Black());
    canvas->drawSimpleText("Shader bundle first frame", 10, 440, font, text);
    text.setShader(
        Shader::MakeLinearGradient({10, 470}, {400, 470}, {Color::Red(), Color::Blue()}));
    canvas->drawSimpleText("Gradient text", 10, 480, font, text);
  }
}

static void PrintStats(const char* label, double ms, const ProgramCacheStats& stats) {
  std::printf(
      "[Bench] mode=%s %s ms=%.2f requests=%llu hits=%llu misses=%llu precompiled=%llu "
      "builder=%llu rtAttempts=%llu rtFailures=%llu\n",
      std::getenv("TGFX_AOT_DISABLE") != nullptr ? "JIT" : "AOT", label, ms,
      static_cast<unsigned long long>(stats.requests),
      static_cast<unsigned long long>(stats.cacheHits),
      static_cast<unsigned long long>(stats.cacheMisses),
      static_cast<unsigned long long>(stats.precompiledArtifactCreations),
      static_cast<unsigned long long>(stats.programBuilderCreations),
      static_cast<unsigned long long>(stats.runtimePipelineCreationAttempts),
      static_cast<unsigned long long>(stats.runtimePipelineCreationFailures));
}

TGFX_TEST(ShaderBundleBenchmark, DISABLED_FirstFrame) {
  auto image = Image::MakeFromFile(ProjectPath::Absolute("resources/apitest/checker_128.png"));
  ASSERT_TRUE(image != nullptr);
  auto typeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSerifSC-Regular.otf"));

  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 700, 520);
  ASSERT_TRUE(surface != nullptr);
  auto* canvas = surface->getCanvas();

  // The test fixture has already created this process's Device, so the first frame below starts
  // from an empty program cache but an initialised driver.
  for (int frame = 1; frame <= 5; frame++) {
    auto start = Clock::Now();
    DrawScene(canvas, image, typeface);
    context->flushAndSubmit(true);
    auto micros = Clock::Now() - start;
    auto label = "frame" + std::to_string(frame);
    PrintStats(label.c_str(), micros / 1000.0, context->globalCache()->programStats());
  }
  std::fflush(stdout);
}

// Draws the scene repeatedly, emptying the program cache before every frame. A cost that is paid
// once per process (decompressing or parsing the bundle) shows up only in the first frame; a cost
// paid per program shows up in every frame.
TGFX_TEST(ShaderBundleBenchmark, DISABLED_ColdFrames) {
  auto image = Image::MakeFromFile(ProjectPath::Absolute("resources/apitest/checker_128.png"));
  ASSERT_TRUE(image != nullptr);
  auto typeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSerifSC-Regular.otf"));
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 700, 520);
  ASSERT_TRUE(surface != nullptr);
  for (int round = 1; round <= 6; round++) {
    // Two frames per round: the second one creates the programs a first frame leaves for later.
    context->globalCache()->clearPrograms();
    auto before = context->globalCache()->programStats();
    auto start = Clock::Now();
    DrawScene(surface->getCanvas(), image, typeface);
    context->flushAndSubmit(true);
    auto firstMicros = Clock::Now() - start;
    start = Clock::Now();
    DrawScene(surface->getCanvas(), image, typeface);
    context->flushAndSubmit(true);
    auto secondMicros = Clock::Now() - start;
    auto after = context->globalCache()->programStats();
    std::printf("[Bench] mode=%s coldRound%d firstMs=%.2f secondMs=%.2f newPrograms=%llu\n",
                std::getenv("TGFX_AOT_DISABLE") != nullptr ? "JIT" : "AOT", round,
                firstMicros / 1000.0, secondMicros / 1000.0,
                static_cast<unsigned long long>(after.cacheMisses - before.cacheMisses));
  }
  std::fflush(stdout);
}

// Steady state: the same scene drawn over and over with every program already cached. The frame is
// split into the time to record and submit the commands (CPU) and the time spent afterwards waiting
// for the GPU to finish, so a per-draw CPU cost can be told apart from slower GPU execution.
TGFX_TEST(ShaderBundleBenchmark, DISABLED_SteadyState) {
  auto image = Image::MakeFromFile(ProjectPath::Absolute("resources/apitest/checker_128.png"));
  ASSERT_TRUE(image != nullptr);
  auto typeface =
      Typeface::MakeFromPath(ProjectPath::Absolute("resources/font/NotoSerifSC-Regular.otf"));
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 700, 520);
  ASSERT_TRUE(surface != nullptr);
  std::vector<double> submitMs;
  std::vector<double> waitMs;
  for (int frame = 0; frame < 200; frame++) {
    auto start = Clock::Now();
    DrawScene(surface->getCanvas(), image, typeface);
    context->flushAndSubmit(false);
    auto submitted = Clock::Now();
    context->flushAndSubmit(true);
    auto done = Clock::Now();
    if (frame >= 20) {
      submitMs.push_back((submitted - start) / 1000.0);
      waitMs.push_back((done - submitted) / 1000.0);
    }
  }
  std::sort(submitMs.begin(), submitMs.end());
  std::sort(waitMs.begin(), waitMs.end());
  std::printf("[Bench] mode=%s steady cpuSubmitMs=%.3f gpuWaitMs=%.3f\n",
              std::getenv("TGFX_AOT_DISABLE") != nullptr ? "JIT" : "AOT",
              submitMs[submitMs.size() / 2], waitMs[waitMs.size() / 2]);
  std::fflush(stdout);
}

// Bytes of physical memory the process currently holds, or 0 where it cannot be read.
static uint64_t ResidentBytes() {
#if defined(__APPLE__)
  task_vm_info_data_t info = {};
  mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
  if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) ==
      KERN_SUCCESS) {
    return info.phys_footprint;
  }
#endif
  return 0;
}

// Loads the embedded bundle into fresh caches and reports what each load costs in time and in
// process memory. A Context does exactly this when it is created (unless TGFX_AOT_DISABLE is set),
// so every Context pays this cost again and holds its own copy. The caches are kept alive so the
// memory they hold adds up.
TGFX_TEST(ShaderBundleBenchmark, DISABLED_BundleLoad) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  std::vector<std::unique_ptr<PrecompiledShaderCache>> caches;
  auto baseline = ResidentBytes();
  for (int i = 0; i < 6; i++) {
    auto before = ResidentBytes();
    auto start = Clock::Now();
    auto cache = std::make_unique<PrecompiledShaderCache>(context, context->backend());
    auto loaded = LoadEmbeddedBundle(cache.get(), context->backend());
    auto micros = Clock::Now() - start;
    ASSERT_TRUE(loaded);
    auto after = ResidentBytes();
    std::printf("[Bench] bundleLoad%d ms=%.2f entries=%zu footprintDeltaKB=%lld totalKB=%lld\n",
                i + 1, micros / 1000.0, cache->entryCount(),
                static_cast<long long>(after - before) / 1024,
                static_cast<long long>(after - baseline) / 1024);
    caches.push_back(std::move(cache));
  }
  auto bundle = CopyEmbeddedBundle(context->backend());
  std::printf("[Bench] embeddedBundleBytes=%zu\n", bundle.size());
  std::fflush(stdout);
}

}  // namespace tgfx

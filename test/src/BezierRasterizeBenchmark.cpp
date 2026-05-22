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

// Performance benchmark for the bezier rasterization render path. Reports timings for the
// CPU-side vertex generation (ShapeBezierRasterizer::getData) and the end-to-end GPU
// pipeline (drawPath + flushAndSubmit), comparing the legacy fallback against the new
// bezier path under the same path inputs. Results are written as CSV to
// `test/out/BezierRasterizeBenchmark/result.csv` and printed as a console summary table.
//
// The benchmark uses a small iteration budget (5 warmup + 30 measure per case) so the
// total runtime stays in the same order as the existing baseline-comparison tests. No
// hard timing thresholds are asserted -- this report is for relative comparison across
// builds, not pass/fail gating.

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <vector>
#include "core/PathTriangulator.h"
#include "core/ShapeBezierRasterizer.h"
#include "core/utils/Log.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/PathTypes.h"
#include "tgfx/core/Shape.h"
#include "tgfx/core/Surface.h"
#include "tgfx/gpu/GPU.h"
#include "tgfx/gpu/GPUFeatures.h"
#include "utils/TestUtils.h"

namespace tgfx {

namespace {

struct BenchmarkCase {
  std::string name;  // Composite key, e.g. "Cubics_64".
  std::string type;  // "lines" / "quads" / "cubics", convenient for CSV grouping.
  int count;         // Number of primitives of that type (lineTo / quadTo / cubicTo calls).
  Path path;
};

// Closed N-gon inscribed in a circle of radius r centred on (cx, cy). Produces N lineTo
// segments after the initial moveTo, so primitive count == N. Used as the "lines" arm of
// the benchmark grid.
static Path BuildScatteredLinesPath(int lineCount) {
  Path path;
  const float cx = 150.0f;
  const float cy = 150.0f;
  const float r = 120.0f;
  for (int i = 0; i < lineCount; ++i) {
    float angle = static_cast<float>(i) * 6.28318530717958647692f / static_cast<float>(lineCount);
    float x = cx + r * std::sin(angle);
    float y = cy - r * std::cos(angle);
    if (i == 0) {
      path.moveTo(x, y);
    } else {
      path.lineTo(x, y);
    }
  }
  path.close();
  return path;
}

// N quadratic-bezier wedges fanning out from the centre, all closed back to the centre to
// form a fillable contour. Stresses the CPU-side decomposition / vertex emission loop with
// a high quad count.
static Path BuildScatteredQuadsPath(int quadCount) {
  Path path;
  const float cx = 150.0f;
  const float cy = 150.0f;
  const float r = 120.0f;
  path.moveTo(cx, cy);
  for (int i = 0; i < quadCount; ++i) {
    float a0 = static_cast<float>(i) * 6.28318530717958647692f / static_cast<float>(quadCount);
    float a1 = static_cast<float>(i + 1) * 6.28318530717958647692f / static_cast<float>(quadCount);
    float am = (a0 + a1) * 0.5f;
    float ctlR = r * 1.3f;
    float cxp = cx + ctlR * std::sin(am);
    float cyp = cy - ctlR * std::cos(am);
    float ex = cx + r * std::sin(a1);
    float ey = cy - r * std::cos(a1);
    path.quadTo(cxp, cyp, ex, ey);
  }
  path.close();
  return path;
}

// N cubic-bezier wedges fanning out from the centre. Cubics are converted to quads by
// PathUtils::ConvertCubicToQuads, so this exercises the cubic-subdivision branch of the
// CPU rasterizer in addition to the standard quad emission.
static Path BuildScatteredCubicsPath(int cubicCount) {
  Path path;
  const float cx = 150.0f;
  const float cy = 150.0f;
  const float r = 120.0f;
  path.moveTo(cx, cy);
  for (int i = 0; i < cubicCount; ++i) {
    float a0 = static_cast<float>(i) * 6.28318530717958647692f / static_cast<float>(cubicCount);
    float a1 = static_cast<float>(i + 1) * 6.28318530717958647692f / static_cast<float>(cubicCount);
    float t1 = a0 + (a1 - a0) * 0.33f;
    float t2 = a0 + (a1 - a0) * 0.67f;
    float ctlR = r * 1.3f;
    float c1x = cx + ctlR * std::sin(t1);
    float c1y = cy - ctlR * std::cos(t1);
    float c2x = cx + ctlR * std::sin(t2);
    float c2y = cy - ctlR * std::cos(t2);
    float ex = cx + r * std::sin(a1);
    float ey = cy - r * std::cos(a1);
    path.cubicTo(c1x, c1y, c2x, c2y, ex, ey);
  }
  path.close();
  return path;
}

struct TimingStats {
  double minUs = 0.0;
  double medianUs = 0.0;
  double meanUs = 0.0;
  int iterations = 0;
};

// Drives a callable for `warmup` untimed iterations followed by `measure` timed iterations,
// then collapses the samples into min / median / mean (microseconds). The callable runs
// synchronously; any GPU work it triggers should be flushed inside the body so the measured
// span actually covers the wait.
template <typename Body>
static TimingStats RunTimed(int warmup, int measure, Body&& body) {
  for (int i = 0; i < warmup; ++i) {
    body();
  }
  std::vector<double> samples;
  samples.reserve(static_cast<size_t>(measure));
  for (int i = 0; i < measure; ++i) {
    int64_t start = Clock::Now();
    body();
    int64_t end = Clock::Now();
    samples.push_back(static_cast<double>(end - start));
  }
  TimingStats stats;
  stats.iterations = measure;
  if (samples.empty()) {
    return stats;
  }
  std::sort(samples.begin(), samples.end());
  stats.minUs = samples.front();
  stats.medianUs = samples[samples.size() / 2];
  double sum = std::accumulate(samples.begin(), samples.end(), 0.0);
  stats.meanUs = sum / static_cast<double>(samples.size());
  return stats;
}

// 3x3 grid: primitive type {lines, quads, cubics} crossed with primitive count
// {16, 64, 512}. Every case is a closed contour fanning out from a common centre, so the
// only deliberate axis of variation is the primitive type and count -- this keeps the
// per-method timings directly comparable across rows of the same N.
static std::vector<BenchmarkCase> BuildBenchmarkCases() {
  std::vector<BenchmarkCase> cases;
  static constexpr int kCounts[] = {16, 64, 512};
  for (int n : kCounts) {
    cases.push_back({"Lines_" + std::to_string(n), "lines", n, BuildScatteredLinesPath(n)});
  }
  for (int n : kCounts) {
    cases.push_back({"Quads_" + std::to_string(n), "quads", n, BuildScatteredQuadsPath(n)});
  }
  for (int n : kCounts) {
    cases.push_back({"Cubics_" + std::to_string(n), "cubics", n, BuildScatteredCubicsPath(n)});
  }
  return cases;
}

}  // namespace

// Measures the CPU-only vertex-generation cost across three methods on the same input
// paths:
//   - bezier_rasterize: ShapeBezierRasterizer::getData() (Loop-Blinn implicit-curve stream)
//   - triangulate_no_aa: PathTriangulator::ToTriangles (interior triangles only)
//   - triangulate_aa:    PathTriangulator::ToAATriangles (interior + AA edge fan)
//
// PathTriangulator is invoked directly so every case runs through the triangulation path,
// bypassing ShapeRasterizer's heuristic that would otherwise route large/dense shapes to a
// bitmap mask. This keeps the comparison strictly between curve-aware vertex emission and
// classical tessellation, which is the actual decision the bezier-rasterize render path
// is competing against.
//
// Results are written to `test/out/BezierRasterizeBenchmark/cpu_rasterize.csv` and printed
// to stdout. The test is independent of GPU caps and creates no GPU context.
TGFX_TEST(BezierRasterizeBenchmark, CpuRasterize) {
  constexpr int kWarmup = 5;
  constexpr int kMeasure = 30;

  auto cases = BuildBenchmarkCases();

  // One CaseResult per benchmark case, holding the three method timings together so the
  // percentage column can be computed against the same case's triangulate_aa baseline.
  struct CaseResult {
    const BenchmarkCase* bc;
    TimingStats bezier;
    TimingStats noAa;
    TimingStats aa;
  };
  std::vector<CaseResult> results;
  results.reserve(cases.size());

  for (const auto& bc : cases) {
    auto shape = Shape::MakeFrom(bc.path);
    ASSERT_TRUE(shape != nullptr);
    ShapeBezierRasterizer rasterizer(shape);
    auto bezierStats = RunTimed(kWarmup, kMeasure, [&]() { (void)rasterizer.getData(); });

    auto bounds = bc.path.getBounds();
    auto noAaStats = RunTimed(kWarmup, kMeasure, [&]() {
      std::vector<float> vertices;
      (void)PathTriangulator::ToTriangles(bc.path, bounds, &vertices);
    });

    auto aaStats = RunTimed(kWarmup, kMeasure, [&]() {
      std::vector<float> vertices;
      (void)PathTriangulator::ToAATriangles(bc.path, bounds, &vertices);
    });

    results.push_back({&bc, bezierStats, noAaStats, aaStats});
  }

  // Percentage of triangulate_aa median (per row, per case). aa baseline is itself 100%.
  // Lower is better. A baseline of 0 µs (extremely fast aa, can happen for trivial paths)
  // is reported as 0 to avoid division by zero noise.
  auto pctOf = [](double value, double baseline) -> double {
    if (baseline <= 0.0) {
      return 0.0;
    }
    return value * 100.0 / baseline;
  };

  auto outDir = ProjectPath::Absolute("test/out/BezierRasterizeBenchmark");
  std::filesystem::create_directories(outDir);
  auto csvPath = outDir + "/cpu_rasterize.csv";
  std::ofstream csv(csvPath);
  EXPECT_TRUE(csv.good()) << "failed to open " << csvPath;
  csv << "case,type,count,method,iterations,min_us,median_us,mean_us,pct_of_aa_median\n";
  csv << std::fixed << std::setprecision(2);
  auto writeRow = [&](const CaseResult& r, const char* method, const TimingStats& s) {
    csv << r.bc->name << "," << r.bc->type << "," << r.bc->count << "," << method << ","
        << s.iterations << "," << s.minUs << "," << s.medianUs << "," << s.meanUs << ","
        << pctOf(s.medianUs, r.aa.medianUs) << "\n";
  };
  for (const auto& r : results) {
    writeRow(r, "bezier_rasterize", r.bezier);
    writeRow(r, "triangulate_no_aa", r.noAa);
    writeRow(r, "triangulate_aa", r.aa);
  }
  csv.close();

  std::cout << "\n=== BezierRasterize CPU Benchmark (" << kMeasure << " iters/case, " << kWarmup
            << " warmup) ===\n";
  std::cout << "(pct = median / triangulate_aa.median; lower is better)\n";
  std::cout << std::left << std::setw(16) << "case" << std::setw(20) << "method" << std::right
            << std::setw(12) << "min(us)" << std::setw(12) << "median(us)" << std::setw(12)
            << "mean(us)" << std::setw(10) << "pct(%)"
            << "\n";
  std::cout << std::string(82, '-') << "\n";
  std::cout << std::fixed << std::setprecision(2);
  auto printRow = [&](const CaseResult& r, const char* method, const TimingStats& s) {
    std::cout << std::left << std::setw(16) << r.bc->name << std::setw(20) << method << std::right
              << std::setw(12) << s.minUs << std::setw(12) << s.medianUs << std::setw(12)
              << s.meanUs << std::setw(10) << pctOf(s.medianUs, r.aa.medianUs) << "\n";
  };
  for (const auto& r : results) {
    printRow(r, "bezier_rasterize", r.bezier);
    printRow(r, "triangulate_no_aa", r.noAa);
    printRow(r, "triangulate_aa", r.aa);
  }
  std::cout << "CSV written to: " << csvPath << "\n" << std::endl;
}

// Measures the end-to-end render cost (drawPath + flushAndSubmit) for each path under both
// the legacy fallback and the new bezier-rasterize pipeline, by toggling the
// `bezierRasterizeSupported` GPU feature flag. Surfaces are recreated per iteration so the
// two passes share the same warm-cache state. Results land in
// `test/out/BezierRasterizeBenchmark/end_to_end.csv`.
TGFX_TEST(BezierRasterizeBenchmark, EndToEnd) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  constexpr int kWarmup = 5;
  constexpr int kMeasure = 30;
  constexpr int kSurfaceSize = 300;

  auto* mutableFeatures = const_cast<GPUFeatures*>(context->gpu()->features());
  bool original = mutableFeatures->bezierRasterizeSupported;

  auto cases = BuildBenchmarkCases();

  struct Row {
    std::string caseName;
    bool bezierEnabled;
    TimingStats stats;
  };
  std::vector<Row> rows;
  rows.reserve(cases.size() * 2);

  for (const auto& bc : cases) {
    for (int pass = 0; pass < 2; ++pass) {
      bool useBezier = (pass == 1);
      mutableFeatures->bezierRasterizeSupported = useBezier;

      auto stats = RunTimed(kWarmup, kMeasure, [&]() {
        auto surface = Surface::Make(context, kSurfaceSize, kSurfaceSize);
        auto canvas = surface->getCanvas();
        canvas->clear(Color{0.f, 0.f, 0.f, 1.f});
        Paint paint;
        paint.setAntiAlias(false);
        paint.setColor(Color{1.f, 0.f, 0.f, 1.f});
        canvas->drawPath(bc.path, paint);
        context->flushAndSubmit();
      });
      rows.push_back({bc.name, useBezier, stats});
    }
  }

  mutableFeatures->bezierRasterizeSupported = original;

  auto outDir = ProjectPath::Absolute("test/out/BezierRasterizeBenchmark");
  std::filesystem::create_directories(outDir);
  auto csvPath = outDir + "/end_to_end.csv";
  std::ofstream csv(csvPath);
  EXPECT_TRUE(csv.good()) << "failed to open " << csvPath;
  csv << "case,bezier_enabled,iterations,min_us,median_us,mean_us\n";
  csv << std::fixed << std::setprecision(2);
  for (const auto& row : rows) {
    csv << row.caseName << "," << (row.bezierEnabled ? "true" : "false") << ","
        << row.stats.iterations << "," << row.stats.minUs << "," << row.stats.medianUs << ","
        << row.stats.meanUs << "\n";
  }
  csv.close();

  std::cout << "\n=== BezierRasterize End-to-End Benchmark (" << kMeasure << " iters/case, "
            << kWarmup << " warmup) ===\n";
  std::cout << std::left << std::setw(16) << "case" << std::setw(10) << "bezier" << std::right
            << std::setw(12) << "min(us)" << std::setw(12) << "median(us)" << std::setw(12)
            << "mean(us)"
            << "\n";
  std::cout << std::string(62, '-') << "\n";
  std::cout << std::fixed << std::setprecision(2);
  for (const auto& row : rows) {
    std::cout << std::left << std::setw(16) << row.caseName << std::setw(10)
              << (row.bezierEnabled ? "true" : "false") << std::right << std::setw(12)
              << row.stats.minUs << std::setw(12) << row.stats.medianUs << std::setw(12)
              << row.stats.meanUs << "\n";
  }
  std::cout << "CSV written to: " << csvPath << "\n" << std::endl;
}

// Single-shot bezier render: builds Cubics_512 path, forces the bezier-rasterize render
// path on, and runs exactly one drawPath + flushAndSubmit. Used as a stable hook for
// dropping ad-hoc log statements deeper in the GPU submission chain (op build, render task
// scheduling, command encoding, etc.) so we can measure per-stage costs without interleaved
// noise from repeated calls.
//
// The flow is warmup -> measure: the first iteration absorbs one-off costs like shader
// compilation, program linking, and lazy resource creation, so the second iteration logs
// reflect steady-state behaviour. Both iterations emit logs (separated by a marker) -- the
// "MEASURE" line is the one to read.
TGFX_TEST(BezierRasterizeBenchmark, SingleBezierDraw) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto* mutableFeatures = const_cast<GPUFeatures*>(context->gpu()->features());
  bool original = mutableFeatures->bezierRasterizeSupported;
  mutableFeatures->bezierRasterizeSupported = true;

  constexpr int kSurfaceSize = 300;
  auto path = BuildScatteredCubicsPath(512);
  auto surface = Surface::Make(context, kSurfaceSize, kSurfaceSize);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();

  Paint paint;
  paint.setAntiAlias(false);
  paint.setColor(Color{1.f, 0.f, 0.f, 1.f});

  for (int i = 0; i < 2; ++i) {
    const char* phase = i == 0 ? "WARMUP" : "MEASURE";
    canvas->clear(Color{0.f, 0.f, 0.f, 1.f});
    canvas->drawPath(path, paint);
    int64_t t0 = Clock::Now();
    context->flushAndSubmit();
    int64_t t1 = Clock::Now();
    LOGI("[SingleBezierDraw] %s flushAndSubmit=%lldus", phase, static_cast<long long>(t1 - t0));
  }

  mutableFeatures->bezierRasterizeSupported = original;
}

// Counterpart to SingleBezierDraw that forces the legacy render path
// (`bezierRasterizeSupported = false`). Identical inputs and call sequence so logs added
// deeper in the GPU submission chain can be diffed side-by-side between the two render
// paths -- handy for spotting which submission stages differ between bezier and legacy.
//
// Same warmup -> measure flow: read the second (MEASURE) record for steady-state numbers.
TGFX_TEST(BezierRasterizeBenchmark, SingleLegacyDraw) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto* mutableFeatures = const_cast<GPUFeatures*>(context->gpu()->features());
  bool original = mutableFeatures->bezierRasterizeSupported;
  mutableFeatures->bezierRasterizeSupported = false;

  constexpr int kSurfaceSize = 300;
  auto path = BuildScatteredCubicsPath(512);
  auto surface = Surface::Make(context, kSurfaceSize, kSurfaceSize);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();

  Paint paint;
  paint.setAntiAlias(false);
  paint.setColor(Color{1.f, 0.f, 0.f, 1.f});

  for (int i = 0; i < 2; ++i) {
    const char* phase = i == 0 ? "WARMUP" : "MEASURE";
    canvas->clear(Color{0.f, 0.f, 0.f, 1.f});
    canvas->drawPath(path, paint);
    int64_t t0 = Clock::Now();
    context->flushAndSubmit();
    int64_t t1 = Clock::Now();
    LOGI("[SingleLegacyDraw] %s flushAndSubmit=%lldus", phase, static_cast<long long>(t1 - t0));
  }

  mutableFeatures->bezierRasterizeSupported = original;
}

// Multi-path batch driver shared by the BatchBezier_* / BatchLegacy_* tests below. Renders
// `pathCount` distinct quads-based paths into a single surface in one flushAndSubmit pass,
// using a warmup + measure pattern. Each path is a small quads-circle translated to a
// random-ish position across a 1000x1000 surface so that:
//   - legacy cannot collapse them via the pendingShape batching path
//     (different shapes => different unique keys)
//   - the surface is large enough that paths do not overlap into a degenerate pixel-thrash
//
// This isolates the per-path fixed cost on both render paths (legacy single-pass triangle
// list draw vs bezier two-pass stencil-and-cover) and shows whether the batch amortises it.
static void RunBatchDraw(const char* tag, bool useBezier, int pathCount) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);

  auto* mutableFeatures = const_cast<GPUFeatures*>(context->gpu()->features());
  bool original = mutableFeatures->bezierRasterizeSupported;
  mutableFeatures->bezierRasterizeSupported = useBezier;

  constexpr int kSurfaceSize = 1000;
  // Use a moderately complex path so per-path CPU work isn't dominant. Quads_64 produced
  // ~3µs / 50µs (bezier) and ~57µs (tri_no_aa) per call in CpuRasterize, leaving room for
  // GPU-side fixed cost to show up.
  auto basePath = BuildScatteredQuadsPath(64);

  // Spread paths across the surface deterministically. Mix translation with a tiny per-path
  // scale (0.95~1.0) so the matrices differ in more than just translation. This defeats the
  // legacy pendingShape fast-path (`MatrixOnlyDiffersInTranslation`) which would otherwise
  // collapse N drawPath calls of the same shape into a single batched op and hide the real
  // per-path cost we want to measure.
  std::vector<Matrix> matrices;
  matrices.reserve(static_cast<size_t>(pathCount));
  constexpr int kStep = 60;
  constexpr int kCols = 15;
  for (int i = 0; i < pathCount; ++i) {
    float x = static_cast<float>((i % kCols) * kStep);
    float y = static_cast<float>((i / kCols) * kStep);
    float s = 0.95f + 0.001f * static_cast<float>(i % 50);
    auto m = Matrix::MakeScale(s, s);
    m.postTranslate(x, y);
    matrices.push_back(m);
  }

  auto surface = Surface::Make(context, kSurfaceSize, kSurfaceSize);
  ASSERT_TRUE(surface != nullptr);
  auto canvas = surface->getCanvas();

  Paint paint;
  paint.setAntiAlias(false);
  paint.setColor(Color{1.f, 0.f, 0.f, 1.f});

  for (int iter = 0; iter < 2; ++iter) {
    const char* phase = iter == 0 ? "WARMUP" : "MEASURE";
    canvas->clear(Color{0.f, 0.f, 0.f, 1.f});
    for (size_t i = 0; i < matrices.size(); ++i) {
      canvas->save();
      canvas->concat(matrices[i]);
      canvas->drawPath(basePath, paint);
      canvas->restore();
    }
    int64_t t0 = Clock::Now();
    context->flushAndSubmit();
    int64_t t1 = Clock::Now();
    LOGI("[%s] %s N=%d flushAndSubmit=%lldus", tag, phase, pathCount,
         static_cast<long long>(t1 - t0));
  }

  mutableFeatures->bezierRasterizeSupported = original;
}

TGFX_TEST(BezierRasterizeBenchmark, BatchBezier_N10) {
  RunBatchDraw("BatchBezier_N10", true, 10);
}
TGFX_TEST(BezierRasterizeBenchmark, BatchBezier_N50) {
  RunBatchDraw("BatchBezier_N50", true, 50);
}
TGFX_TEST(BezierRasterizeBenchmark, BatchBezier_N200) {
  RunBatchDraw("BatchBezier_N200", true, 200);
}
TGFX_TEST(BezierRasterizeBenchmark, BatchLegacy_N10) {
  RunBatchDraw("BatchLegacy_N10", false, 10);
}
TGFX_TEST(BezierRasterizeBenchmark, BatchLegacy_N50) {
  RunBatchDraw("BatchLegacy_N50", false, 50);
}
TGFX_TEST(BezierRasterizeBenchmark, BatchLegacy_N200) {
  RunBatchDraw("BatchLegacy_N200", false, 200);
}

}  // namespace tgfx

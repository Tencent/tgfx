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
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. See the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

// Diagnostic baselines for the shader-architecture work, not a regression gate: the per-stage cost
// of resolving one draw's program, the program identity of a fixed shape set, and the family
// distribution a fixed draw corpus produces. Every case asserts only that the measurement itself
// succeeded, so the printed numbers can be compared across revisions:
//   TGFXFullTest_OpenGL --gtest_filter='DiagnosticShaderProfile.*'
// Set TGFX_PROFILE_DIR to also write the tables into that directory for a cross-revision diff.

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "base/TGFXTest.h"
#include "core/utils/BlockAllocator.h"
#include "gpu/ContextInternal.h"
#include "gpu/GlobalCache.h"
#include "gpu/ProgramInfo.h"
#include "gpu/ProxyProvider.h"
#include "gpu/aot/bundle/PrecompiledShaderCache.h"
#include "gpu/aot/ir/DrawIR.h"
#include "gpu/aot/ir/GeometryLayout.h"
#include "gpu/aot/select/DrawFacts.h"
#include "gpu/aot/select/FamilyMatch.h"
#include "gpu/aot/select/ProgramKey.h"
#include "gpu/aot/select/ProgramSelection.h"
#include "gpu/aot/select/ShapeKey.h"
#include "gpu/processors/DeviceSpaceTextureEffect.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/processors/RectEffect.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/resources/RenderTarget.h"
#include "gpu/shaders/Families.h"
#include "gpu/shaders/Layout.h"
#include "tgfx/core/Bitmap.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/ColorFilter.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Shader.h"
#include "tgfx/core/Surface.h"
#include "tgfx/gpu/Context.h"
#include "utils/ContextScope.h"
#include "utils/TestUtils.h"

namespace tgfx {
namespace {

using SteadyClock = std::chrono::steady_clock;

// Repeated-resolution timings: the steady-state case, where the draw's program is already cached.
constexpr int HotIterations = 1000;
// Fresh-build timings: every iteration builds a new expression, so the chain compile and the
// proxy work are part of the number. Kept low because each iteration allocates proxies.
constexpr int FreshIterations = 100;

constexpr int ProbeSize = 64;

/// The fixed shape set the stage-cost and identity cases measure. Each shape is served by a
/// different family, so a per-family cost change moves exactly one row.
enum class ProbeShape : uint8_t {
  SolidColor = 0,
  QuadColor = 1,
  QuadTexture = 2,
  ChainComposedCoverage = 3,
  HairlineLine = 4,
};

constexpr ProbeShape ProbeShapes[] = {
    ProbeShape::SolidColor,   ProbeShape::QuadColor,
    ProbeShape::QuadTexture,  ProbeShape::ChainComposedCoverage,
    ProbeShape::HairlineLine,
};

const char* ProbeName(ProbeShape shape) {
  switch (shape) {
    case ProbeShape::SolidColor:
      return "SolidColor";
    case ProbeShape::QuadColor:
      return "QuadColor";
    case ProbeShape::QuadTexture:
      return "QuadTexture";
    case ProbeShape::ChainComposedCoverage:
      return "ChainComposedCoverage";
    case ProbeShape::HairlineLine:
      return "HairlineLine";
  }
  return "Unknown";
}

/// One probe's built expression: the geometry layout the op would have made and the lowered IR.
/// The IR's nodes live on the caller's BlockAllocator, so it must outlive this value.
struct ProbeBuild {
  GeometryLayout geometry = {};
  std::optional<DrawIR> ir = std::nullopt;
};

bool BuildProbe(Context* context, ProbeShape shape, BlockAllocator* allocator, ProbeBuild* build) {
  switch (shape) {
    case ProbeShape::SolidColor: {
      build->geometry =
          DefaultGeometryLayout(PMColor::White(), AAType::Coverage, Matrix::I(), Matrix::I());
      auto clip = RectEffect::Make(allocator, Rect::MakeWH(ProbeSize, ProbeSize));
      if (clip == nullptr) {
        return false;
      }
      build->ir = LowerDraw({}, {clip.get()}, BlendMode::SrcOver, allocator);
      return build->ir.has_value();
    }
    case ProbeShape::QuadColor: {
      build->geometry = QuadGeometryLayoutOf(ProbeSize, ProbeSize, AAType::Coverage,
                                             PMColor::White(), Matrix::I(), false);
      auto clip = RectEffect::Make(allocator, Rect::MakeWH(ProbeSize, ProbeSize));
      if (clip == nullptr) {
        return false;
      }
      build->ir = LowerDraw({}, {clip.get()}, BlendMode::SrcOver, allocator);
      return build->ir.has_value();
    }
    case ProbeShape::QuadTexture: {
      build->geometry = QuadGeometryLayoutOf(ProbeSize, ProbeSize, AAType::Coverage,
                                             PMColor::White(), Matrix::I(), false);
      auto textureProxy = context->proxyProvider()->createTextureProxy({}, ProbeSize, ProbeSize,
                                                                       PixelFormat::RGBA_8888);
      if (textureProxy == nullptr) {
        return false;
      }
      auto color = TextureEffect::Make(allocator, std::move(textureProxy));
      auto clip = RectEffect::Make(allocator, Rect::MakeWH(ProbeSize, ProbeSize));
      if (color == nullptr || clip == nullptr) {
        return false;
      }
      build->ir = LowerDraw({color.get()}, {clip.get()}, BlendMode::SrcOver, allocator);
      return build->ir.has_value();
    }
    case ProbeShape::ChainComposedCoverage: {
      build->geometry =
          DefaultGeometryLayout(PMColor::White(), AAType::None, Matrix::I(), Matrix::I());
      auto maskProxy = context->proxyProvider()->createTextureProxy({}, ProbeSize, ProbeSize,
                                                                    PixelFormat::ALPHA_8);
      if (maskProxy == nullptr) {
        return false;
      }
      auto mask = DeviceSpaceTextureEffect::Make(allocator, std::move(maskProxy), Matrix::I());
      auto clip = RectEffect::Make(allocator, Rect::MakeWH(ProbeSize, ProbeSize));
      if (mask == nullptr || clip == nullptr) {
        return false;
      }
      auto composed = FragmentProcessor::Compose(allocator, std::move(mask), std::move(clip));
      if (composed == nullptr) {
        return false;
      }
      build->ir = LowerDraw({}, {composed.get()}, BlendMode::SrcOver, allocator);
      return build->ir.has_value();
    }
    case ProbeShape::HairlineLine: {
      build->geometry = HairlineLineGeometryLayout(PMColor::White(), Matrix::I(), Matrix::I(), 1.0f,
                                                   AAType::Coverage);
      auto clip = RectEffect::Make(allocator, Rect::MakeWH(ProbeSize, ProbeSize));
      if (clip == nullptr) {
        return false;
      }
      build->ir = LowerDraw({}, {clip.get()}, BlendMode::SrcOver, allocator);
      return build->ir.has_value();
    }
  }
  return false;
}

uint64_t ElapsedNanos(SteadyClock::time_point from, SteadyClock::time_point to) {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(to - from).count());
}

double PerIteration(uint64_t totalNanos, int iterations) {
  return static_cast<double>(totalNanos) / static_cast<double>(iterations);
}

/// A stable 64-bit digest of a key string, so a snapshot line stays diffable instead of carrying a
/// few hundred hex characters. FNV-1a: no dependency, and collisions do not matter here.
uint64_t DigestOf(const std::string& value) {
  uint64_t hash = 1469598103934665603ull;
  for (auto character : value) {
    hash ^= static_cast<uint64_t>(static_cast<uint8_t>(character));
    hash *= 1099511628211ull;
  }
  return hash;
}

std::string FormatLine(const char* format, ...) {
  char buffer[512] = {};
  va_list args;
  va_start(args, format);
  std::vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  return std::string(buffer);
}

void Report(const std::vector<std::string>& lines) {
  for (const auto& line : lines) {
    std::printf("%s\n", line.c_str());
  }
}

void WriteReport(const char* fileName, const std::vector<std::string>& lines) {
  auto* directory = std::getenv("TGFX_PROFILE_DIR");
  if (directory == nullptr) {
    return;
  }
  std::string path = std::string(directory) + "/" + fileName;
  auto* file = std::fopen(path.c_str(), "w");
  if (file == nullptr) {
    std::printf("[SP] could not open %s\n", path.c_str());
    return;
  }
  for (const auto& line : lines) {
    std::fprintf(file, "%s\n", line.c_str());
  }
  std::fclose(file);
}

bool PrepareContext(Context* context, PrecompiledShaderCache* cache) {
  auto [bundleData, bundleBytes] = GetEmbeddedBundle(context->backend());
  if (bundleData == nullptr || bundleBytes == 0) {
    return false;
  }
  if (!cache->loadBundle(bundleData, bundleBytes)) {
    return false;
  }
  context->globalCache()->clearPrograms();
  return true;
}

/// A fixed canvas corpus covering the families most draws land on: solid fills, gradients,
/// textures, rounded rects, hairlines, a color filter, a blend mode and a clip.
void DrawCorpus(Canvas* canvas) {
  Paint paint = {};
  paint.setColor(Color::Red());
  canvas->drawRect(Rect::MakeXYWH(4, 4, 60, 60), paint);

  paint.setColor(Color::FromRGBA(32, 64, 200, 128));
  paint.setBlendMode(BlendMode::Multiply);
  canvas->drawRect(Rect::MakeXYWH(30, 30, 60, 60), paint);

  paint = {};
  paint.setShader(Shader::MakeLinearGradient(Point::Make(0, 0), Point::Make(0, 96),
                                             {Color::Red(), Color::Blue()}));
  canvas->drawRect(Rect::MakeXYWH(100, 4, 60, 96), paint);

  paint = {};
  paint.setShader(Shader::MakeRadialGradient(Point::Make(200, 60), 40,
                                             {Color::Blue(), Color::Green(), Color::Red()}));
  canvas->drawRect(Rect::MakeXYWH(170, 10, 60, 100), paint);

  auto image = MakeImage("resources/apitest/imageReplacement.png");
  if (image != nullptr) {
    paint = {};
    paint.setShader(Shader::MakeImageShader(image));
    canvas->drawRect(Rect::MakeXYWH(100, 110, 60, 60), paint);
  }

  paint = {};
  paint.setColor(Color::Green());
  canvas->drawRRect(RRect::MakeRectXY(Rect::MakeXYWH(4, 110, 60, 60), 12, 12), paint);

  paint.setStyle(PaintStyle::Stroke);
  paint.setStrokeWidth(0);
  canvas->drawRRect(RRect::MakeRectXY(Rect::MakeXYWH(100, 180, 60, 60), 10, 10), paint);

  paint = {};
  paint.setColor(Color::Red());
  paint.setColorFilter(
      ColorFilter::Matrix({1, 0, 0, 0, 0.1f, 0, 1, 0, 0, 0.1f, 0, 0, 1, 0, 0.1f, 0, 0, 0, 1, 0}));
  canvas->drawRect(Rect::MakeXYWH(170, 120, 60, 50), paint);

  paint = {};
  paint.setColor(Color::Blue());
  canvas->drawOval(Rect::MakeXYWH(170, 180, 60, 50), paint);

  canvas->save();
  canvas->clipRect(Rect::MakeXYWH(4, 180, 120, 60));
  paint = {};
  paint.setShader(Shader::MakeLinearGradient(Point::Make(0, 180), Point::Make(120, 240),
                                             {Color::Green(), Color::Blue()}));
  canvas->drawRect(Rect::MakeXYWH(4, 180, 120, 60), paint);
  canvas->restore();
}

}  // namespace

// Per-stage cost of resolving one draw's program: the shape extraction, the structural key, the
// family walk, the whole selection, the program key and the cached lookup. The fresh row rebuilds
// the expression each iteration, so it carries the lower plus the chain compile a new draw pays.
TGFX_TEST(DiagnosticShaderProfile, SelectStageCost) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto* cache = ContextCacheAccess::Cache(context);
  ASSERT_NE(cache, nullptr);
  if (!PrepareContext(context, cache)) {
    std::printf("[SP][cost] no embedded bundle for %s, skipped\n", TGFX_BACKEND_NAME);
    return;
  }
  auto renderTargetProxy = RenderTargetProxy::Make(context, ProbeSize, ProbeSize, false);
  ASSERT_NE(renderTargetProxy, nullptr);
  auto renderTarget = renderTargetProxy->getRenderTarget();
  ASSERT_NE(renderTarget, nullptr);

  std::vector<std::string> report;
  report.push_back("[SP][cost] ns/op, hot = cached program, fresh = new expression per draw");
  for (auto probe : ProbeShapes) {
    BlockAllocator allocator;
    ProbeBuild build = {};
    ASSERT_TRUE(BuildProbe(context, probe, &allocator, &build)) << ProbeName(probe);
    ASSERT_TRUE(build.ir.has_value());
    ProgramInfo info(renderTarget.get(), build.geometry, std::nullopt, BlendMode::SrcOver, false);
    info.setCarriedIR(&*build.ir);
    auto failure = AOTFailure::None;
    auto selection = SelectProgram(context, info, &failure);
    ASSERT_TRUE(selection.has_value())
        << ProbeName(probe) << " matched nothing, failure=" << static_cast<int>(failure);
    ASSERT_NE(info.getProgram(), nullptr) << ProbeName(probe);
    const uint32_t variantId = selection->variant.id();

    uint64_t shapeNanos = 0;
    uint64_t keyNanos = 0;
    uint64_t matchNanos = 0;
    uint64_t selectNanos = 0;
    uint64_t programKeyNanos = 0;
    uint64_t programNanos = 0;
    size_t sink = 0;
    for (int i = 0; i < HotIterations; ++i) {
      auto start = SteadyClock::now();
      auto drawShape = ExtractDrawShape(&info);
      auto afterShape = SteadyClock::now();
      auto shapeKey = MakeShapeKey(drawShape, info);
      auto afterKey = SteadyClock::now();
      auto match = MatchAssembledFamily(&info, drawShape);
      auto afterMatch = SteadyClock::now();
      auto selected = SelectProgram(context, info, &failure);
      auto afterSelect = SteadyClock::now();
      auto programKey = MakeProgramKey(context, info, variantId, selection->spec);
      auto afterProgramKey = SteadyClock::now();
      shapeNanos += ElapsedNanos(start, afterShape);
      keyNanos += ElapsedNanos(afterShape, afterKey);
      matchNanos += ElapsedNanos(afterKey, afterMatch);
      selectNanos += ElapsedNanos(afterMatch, afterSelect);
      programKeyNanos += ElapsedNanos(afterSelect, afterProgramKey);
      sink += shapeKey.size() + programKey.size();
      sink += match.has_value() ? 1 : 0;
      sink += selected.has_value() ? 1 : 0;
    }
    for (int i = 0; i < HotIterations; ++i) {
      ProgramInfo fresh(renderTarget.get(), build.geometry, std::nullopt, BlendMode::SrcOver,
                        false);
      fresh.setCarriedIR(&*build.ir);
      auto start = SteadyClock::now();
      auto program = fresh.getProgram();
      auto end = SteadyClock::now();
      programNanos += ElapsedNanos(start, end);
      sink += program != nullptr ? 1 : 0;
    }
    uint64_t freshNanos = 0;
    int freshBuilt = 0;
    for (int i = 0; i < FreshIterations; ++i) {
      BlockAllocator freshAllocator;
      ProbeBuild freshBuild = {};
      auto start = SteadyClock::now();
      auto built = BuildProbe(context, probe, &freshAllocator, &freshBuild);
      auto end = SteadyClock::now();
      freshNanos += ElapsedNanos(start, end);
      freshBuilt += built ? 1 : 0;
    }
    EXPECT_GT(sink, 0u) << ProbeName(probe);
    EXPECT_GT(selectNanos, 0u) << ProbeName(probe);
    EXPECT_EQ(freshBuilt, FreshIterations) << ProbeName(probe);

    report.push_back(FormatLine(
        "[SP][cost] %-22s family=%-12s shape=%7.1f key=%7.1f match=%7.1f select=%7.1f "
        "programKey=%7.1f getProgram=%7.1f fresh=%8.1f",
        ProbeName(probe), Families()[selection->variant.family].name,
        PerIteration(shapeNanos, HotIterations), PerIteration(keyNanos, HotIterations),
        PerIteration(matchNanos, HotIterations), PerIteration(selectNanos, HotIterations),
        PerIteration(programKeyNanos, HotIterations), PerIteration(programNanos, HotIterations),
        PerIteration(freshNanos, FreshIterations)));
  }
  Report(report);
  WriteReport("select-cost.txt", report);
}

// The program identity of every probe: which family serves it, the variant id, the derived uniform
// block sizes and the texture count. This is the structure-level snapshot the writer and lowering
// refactors must keep unchanged; uniform values are covered by the screenshot suite instead.
TGFX_TEST(DiagnosticShaderProfile, ProgramIdentity) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto* cache = ContextCacheAccess::Cache(context);
  ASSERT_NE(cache, nullptr);
  if (!PrepareContext(context, cache)) {
    std::printf("[SP][identity] no embedded bundle for %s, skipped\n", TGFX_BACKEND_NAME);
    return;
  }
  auto renderTargetProxy = RenderTargetProxy::Make(context, ProbeSize, ProbeSize, false);
  ASSERT_NE(renderTargetProxy, nullptr);
  auto renderTarget = renderTargetProxy->getRenderTarget();
  ASSERT_NE(renderTarget, nullptr);

  std::vector<std::string> report;
  report.push_back("[SP][identity] probe family variant vBlock fBlock textures key");
  for (auto probe : ProbeShapes) {
    BlockAllocator allocator;
    ProbeBuild build = {};
    ASSERT_TRUE(BuildProbe(context, probe, &allocator, &build)) << ProbeName(probe);
    ProgramInfo info(renderTarget.get(), build.geometry, std::nullopt, BlendMode::SrcOver, false);
    info.setCarriedIR(&*build.ir);
    auto failure = AOTFailure::None;
    auto selection = SelectProgram(context, info, &failure);
    ASSERT_TRUE(selection.has_value()) << ProbeName(probe);
    auto program = info.getProgram();
    ASSERT_NE(program, nullptr) << ProbeName(probe);
    const auto layout = DeriveLayout(selection->variant);
    const auto key = ProgramInfo::FormatDiagnosticKey(info.programKey());
    EXPECT_GT(static_cast<size_t>(layout.vertexBlockSize) + layout.fragmentBlockSize, 0u)
        << ProbeName(probe);

    ProgramInfo duplicate(renderTarget.get(), build.geometry, std::nullopt, BlendMode::SrcOver,
                          false);
    duplicate.setCarriedIR(&*build.ir);
    auto duplicateFailure = AOTFailure::None;
    auto duplicateSelection = SelectProgram(context, duplicate, &duplicateFailure);
    ASSERT_TRUE(duplicateSelection.has_value()) << ProbeName(probe);
    EXPECT_EQ(duplicateSelection->variant.id(), selection->variant.id()) << ProbeName(probe);
    EXPECT_EQ(ProgramInfo::FormatDiagnosticKey(duplicate.programKey()), key) << ProbeName(probe);

    report.push_back(FormatLine(
        "[SP][identity] %-22s %-14s variant=%7u vBlock=%4u fBlock=%5u "
        "textures=%u key=%016llx keyLength=%zu",
        ProbeName(probe), Families()[selection->variant.family].name, selection->variant.id(),
        layout.vertexBlockSize, layout.fragmentBlockSize, program->expectedTextureCount(),
        static_cast<unsigned long long>(DigestOf(key)), key.size()));
  }
  Report(report);
  WriteReport("program-identity.txt", report);
}

// The family distribution a fixed canvas corpus produces: which families serve the draws, how many
// distinct variants they create, and how much of the corpus rides the chain interpreter. This is
// the picture the decomposition and chain decisions are read against.
TGFX_TEST(DiagnosticShaderProfile, CorpusDistribution) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto* cache = ContextCacheAccess::Cache(context);
  ASSERT_NE(cache, nullptr);
  if (!PrepareContext(context, cache)) {
    std::printf("[SP][family] no embedded bundle for %s, skipped\n", TGFX_BACKEND_NAME);
    return;
  }
  auto surface = Surface::Make(context, 256, 256);
  ASSERT_NE(surface, nullptr);
  const bool wasRecording = cache->diagnosticRecordingEnabled();
  cache->setDiagnosticRecordingEnabled(true);
  cache->resetStats();
  context->globalCache()->resetProgramStats();
  DrawCorpus(surface->getCanvas());
  context->flushAndSubmit(true);

  const auto drawStats = cache->drawStats();
  const auto programStats = context->globalCache()->programStats();
  std::map<std::string, size_t> variantsPerFamily = {};
  size_t hitRecords = 0;
  for (const auto& record : cache->hitRecords()) {
    ++hitRecords;
    const uint16_t familyIndex = static_cast<uint16_t>(record.variantId >> 20);
    const std::string name = familyIndex < FamilyCount() ? Families()[familyIndex].name : "Unknown";
    ++variantsPerFamily[name];
  }
  cache->setDiagnosticRecordingEnabled(wasRecording);

  std::vector<std::string> report;
  report.push_back("[SP][family] distinct variants created per family");
  for (const auto& entry : variantsPerFamily) {
    report.push_back(FormatLine("[SP][family] %-32s %3zu", entry.first.c_str(), entry.second));
  }
  report.push_back(FormatLine(
      "[SP][draws] total=%llu aotComplete=%llu chainDirect=%llu offscreenPlan=%llu dropped=%llu",
      static_cast<unsigned long long>(drawStats.draws),
      static_cast<unsigned long long>(drawStats.completeAOTDraws),
      static_cast<unsigned long long>(drawStats.directChainDraws),
      static_cast<unsigned long long>(drawStats.offscreenPlanDraws),
      static_cast<unsigned long long>(drawStats.droppedDraws)));
  report.push_back(
      FormatLine("[SP][cache] requests=%llu hits=%llu misses=%llu creations=%llu",
                 static_cast<unsigned long long>(programStats.requests),
                 static_cast<unsigned long long>(programStats.cacheHits),
                 static_cast<unsigned long long>(programStats.cacheMisses),
                 static_cast<unsigned long long>(programStats.precompiledArtifactCreations)));
  EXPECT_GT(drawStats.draws, 0u);
  EXPECT_EQ(drawStats.droppedDraws, 0u);
  EXPECT_GT(hitRecords, 0u);
  EXPECT_GT(programStats.precompiledArtifactCreations, 0u);
  Report(report);
  WriteReport("corpus-distribution.txt", report);
}

}  // namespace tgfx

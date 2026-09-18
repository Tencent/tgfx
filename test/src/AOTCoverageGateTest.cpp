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

// The effect-coverage gate (WP5): every processor class must be claimed by the precompiled
// system before it can ship. DEFINE_PROCESSOR_CLASS_ID is a mandatory marker (a processor cannot
// construct without a ClassID), so scanning the processor headers for it enumerates the full
// type set; a newly added class that nobody claimed fails here instead of reaching users as an
// uncovered effect that silently falls back to runtime compilation.
//
// A class is claimed when at least one of the following holds:
//   - kMatcher: a matcher rule serves draws containing it (verified by the name() string or a
//     qualified `ClassName::` reference appearing in PermutationMatcher.cpp);
//   - kLowering: the class declares lowerToAOT (verified in its header);
//   - kStructural: it is consumed inside another processor's claim — gradient layouts and
//     colorizers, the glass geometry children — and the matcher references its name to
//     discriminate the parent's shape;
//   - none of the above (runtime by design), which requires a documented reason.
// AOTClosureVerifier covers a different axis (registered shaders' reachable sets fit inside the
// published bundles); this gate covers the processor-type axis and the two checks coexist.

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>
#include "base/TGFXTest.h"
#include "gpu/GlobalCache.h"
#include "gpu/PrecompiledShaderCache.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/ColorFilter.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Surface.h"
#include "utils/ContextScope.h"
#include "utils/ProjectPath.h"
#include "utils/ShaderAOTTestReporter.h"
#include "utils/TestUtils.h"

namespace tgfx {
namespace {

constexpr uint32_t kMatcher = 1;
constexpr uint32_t kLowering = 2;
constexpr uint32_t kStructural = 4;

struct ProcessorClaim {
  const char* className = "";
  const char* nameString = "";
  uint32_t claims = 0;
  const char* note = "";
};

// The claim manifest. Keep in sync with the processor headers: adding a class without a claim
// entry (or a reason) fails the gate, and removing a class leaves a stale entry that also fails.
const ProcessorClaim ProcessorClaims[] = {
    // Geometry processors, each served by matcher rules for its draw shapes.
    {"AtlasTextGeometryProcessor", "AtlasTextGeometryProcessor", kMatcher, ""},
    {"ComplexEllipseGeometryProcessor", "ComplexEllipseGeometryProcessor", kMatcher, ""},
    {"ComplexNonAARRectGeometryProcessor", "ComplexNonAARRectGeometryProcessor", kMatcher, ""},
    {"DefaultGeometryProcessor", "DefaultGeometryProcessor", kMatcher, ""},
    {"EllipseGeometryProcessor", "EllipseGeometryProcessor", kMatcher, ""},
    {"HairlineLineGeometryProcessor", "HairlineLineGeometryProcessor", kMatcher, ""},
    {"HairlineQuadGeometryProcessor", "HairlineQuadGeometryProcessor", kMatcher, ""},
    {"MeshGeometryProcessor", "MeshGeometryProcessor", kMatcher, ""},
    {"NonAARRectGeometryProcessor", "NonAARRectGeometryProcessor", kMatcher, ""},
    {"QuadPerEdgeAAGeometryProcessor", "QuadPerEdgeAAGeometryProcessor", kMatcher, ""},
    {"RoundStrokeRectGeometryProcessor", "RoundStrokeRectGeometryProcessor", kMatcher, ""},
    {"ShapeInstancedGeometryProcessor", "ShapeInstancedGeometryProcessor", kMatcher, ""},
    {"StencilCoverCoverPassGeometryProcessor", "StencilCoverCoverPassGeometryProcessor", 0,
     "the stencil cover path sits behind TGFX_ENABLE_STENCIL_COVER_PATH (off by default) and is "
     "not an AOT route"},
    {"StencilCoverStencilPassGeometryProcessor", "StencilCoverStencilPassGeometryProcessor", 0,
     "the stencil cover path sits behind TGFX_ENABLE_STENCIL_COVER_PATH (off by default) and is "
     "not an AOT route"},
    // Xfer processors: EmptyXferProcessor is claimed through its singleton identity
    // (EmptyXferProcessor::GetInstance in GetXPType) rather than a name() string.
    {"EmptyXferProcessor", "EmptyXferProcessor", kMatcher, ""},
    {"PorterDuffXferProcessor", "PorterDuffXferProcessor", kMatcher, ""},
    // Fragment processors carrying an AOT lowering; most are also matcher-served at the top
    // level. AlphaThresholdFragmentProcessor's name() is "AlphaStepFragmentProcessor" (the name
    // predates the class rename), and XfermodeFragmentProcessor's name() carries a
    // " - dst"/" - src" suffix.
    {"AlphaThresholdFragmentProcessor", "AlphaStepFragmentProcessor", kMatcher | kLowering, ""},
    {"ClampedGradientEffect", "ClampedGradientEffect", kMatcher | kLowering, ""},
    {"ColorMatrixFragmentProcessor", "ColorMatrixFragmentProcessor", kMatcher | kLowering, ""},
    {"ColorSpaceXformEffect", "ColorSpaceXformEffect", kMatcher | kLowering, ""},
    {"ComposeFragmentProcessor", "ComposeFragmentProcessor", kLowering,
     "structural container: lowers by chaining its children"},
    {"ConstColorProcessor", "ConstColorProcessor", kLowering,
     "served as a chain slot through the lowering"},
    {"DeviceSpaceTextureEffect", "DeviceSpaceTextureEffect", kMatcher | kLowering, ""},
    {"AOTYUVChainProcessor", "AOTYUVChainProcessor", kMatcher,
     "the YUV color-grade route: matcher-claimed through TryMatchYUVTextureFill"},
    {"LumaFragmentProcessor", "LumaFragmentProcessor", kMatcher | kLowering, ""},
    {"PerlinNoiseFragmentProcessor", "PerlinNoiseFragmentProcessor", kMatcher | kLowering, ""},
    {"RectEffect", "RectEffect", kMatcher | kLowering, ""},
    {"RRectEffect", "RRectEffect", kMatcher | kLowering, ""},
    {"TextureEffect", "TextureEffect", kMatcher | kLowering, ""},
    {"TiledTextureEffect", "TiledTextureEffect", kMatcher | kLowering, ""},
    {"XfermodeFragmentProcessor", "XfermodeFragmentProcessor", kMatcher | kLowering,
     "the SrcIn local-mask coverage form is matcher-claimed; two-processor blends lower"},
    // Structural parts consumed inside ClampedGradientEffect and GlassRefractionShader's
    // matcher rule; the matcher references their names to discriminate the parent's shape.
    {"ConicGradientLayout", "ConicGradientLayout", kStructural, ""},
    {"DiamondGradientLayout", "DiamondGradientLayout", kStructural, ""},
    {"LinearGradientLayout", "LinearGradientLayout", kStructural, ""},
    {"RadialGradientLayout", "RadialGradientLayout", kStructural, ""},
    {"SingleIntervalGradientColorizer", "SingleIntervalGradientColorizer", kStructural, ""},
    {"DualIntervalGradientColorizer", "DualIntervalGradientColorizer", kStructural, ""},
    {"TextureGradientColorizer", "TextureGradientColorizer", kStructural, ""},
    {"UnrolledBinaryGradientColorizer", "UnrolledBinaryGradientColorizer", kStructural, ""},
    {"GlassSDFGeometryFragmentProcessor", "GlassSDFGeometryFragmentProcessor", kStructural, ""},
    {"GlassUDFGeometryFragmentProcessor", "GlassUDFGeometryFragmentProcessor", kStructural, ""},
    // Executor-rebuilt processors matched by the fused kernel rules, and effect processors
    // served by dedicated matcher rules.
    {"AOTPointwiseChainProcessor", "AOTPointwiseChainProcessor", kMatcher, ""},
    {"AOTPointwiseTailProcessor", "AOTPointwiseTailProcessor", kMatcher, ""},
    {"GaussianBlur1DFragmentProcessor", "GaussianBlur1DFragmentProcessor", kMatcher, ""},
    {"GlassRefractionFragmentProcessor", "GlassRefractionFragmentProcessor", kMatcher, ""},
    {"GlassUDFTentBlurFragmentProcessor", "GlassUDFTentBlurFragmentProcessor", kMatcher, ""},
};

struct ScannedProcessor {
  std::string className;
  std::string filePath;
  bool hasLowerToAOT = false;
};

static std::string ReadTextFile(const std::string& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream.is_open()) {
    return {};
  }
  std::ostringstream buffer = {};
  buffer << stream.rdbuf();
  return buffer.str();
}

// Returns the name of the class declaration (`class X ... {`) that most closely precedes the
// given position, or an empty string when none exists. The line-anchored pattern skips forward
// declarations (they carry a semicolon before any brace) and `friend class` mentions (they have
// a `friend ` prefix).
static std::string OwningClassBefore(const std::string& text, size_t position) {
  static const std::regex classPattern =
      std::regex("^\\s*class\\s+(\\w+)[^{;]*\\{", std::regex_constants::multiline);
  std::string owner = {};
  auto begin = std::sregex_iterator(text.begin(), text.end(), classPattern);
  auto end = std::sregex_iterator();
  for (auto it = begin; it != end; ++it) {
    if (it->position() >= static_cast<int>(position)) {
      break;
    }
    owner = (*it)[1].str();
  }
  return owner;
}

static bool LineStartsWithDefine(const std::string& text, size_t position) {
  auto lineStart = text.find_last_of('\n', position);
  lineStart = lineStart == std::string::npos ? 0 : lineStart + 1;
  return text.compare(lineStart, 8, "#define ") == 0;
}

static void ScanProcessorHeader(const std::filesystem::path& path,
                                std::vector<ScannedProcessor>* out) {
  auto text = ReadTextFile(path.string());
  if (text.empty()) {
    return;
  }
  std::vector<std::string> macroOwners = {};
  std::vector<std::string> loweringOwners = {};
  size_t position = 0;
  while ((position = text.find("DEFINE_PROCESSOR_CLASS_ID", position)) != std::string::npos) {
    if (!LineStartsWithDefine(text, position)) {
      auto owner = OwningClassBefore(text, position);
      if (!owner.empty()) {
        macroOwners.push_back(owner);
      }
    }
    ++position;
  }
  position = 0;
  while ((position = text.find("bool lowerToAOT", position)) != std::string::npos) {
    auto owner = OwningClassBefore(text, position);
    if (!owner.empty()) {
      loweringOwners.push_back(owner);
    }
    ++position;
  }
  for (const auto& owner : macroOwners) {
    ScannedProcessor processor = {};
    processor.className = owner;
    processor.filePath = path.filename().string();
    for (const auto& loweringOwner : loweringOwners) {
      if (loweringOwner == owner) {
        processor.hasLowerToAOT = true;
        break;
      }
    }
    out->push_back(std::move(processor));
  }
}

static std::vector<ScannedProcessor> ScanProcessorHeaders() {
  std::vector<ScannedProcessor> result = {};
  for (const auto* directory :
       {"src/gpu/processors", "src/layers/processors", "src/core/shaders", "src/core/filters"}) {
    auto dirPath = std::filesystem::path(ProjectPath::Absolute(directory));
    if (!std::filesystem::exists(dirPath)) {
      continue;
    }
    std::error_code error = {};
    for (auto it = std::filesystem::directory_iterator(dirPath, error);
         it != std::filesystem::directory_iterator(); it.increment(error)) {
      if (error || !it->is_regular_file() || it->path().extension() != ".h") {
        continue;
      }
      ScanProcessorHeader(it->path(), &result);
    }
  }
  return result;
}

// The pure gate check, factored out so synthetic violations can verify the gate itself. A
// matcher/structural claim is proven by the name() string or a qualified `ClassName::` reference
// (the singleton identity form) appearing in the matcher source; a lowering claim by the scanned
// header declaring lowerToAOT; a runtime-by-design entry by its documented reason.
static std::vector<std::string> EvaluateProcessorClaims(
    const std::vector<ScannedProcessor>& scanned, const ProcessorClaim* claims, size_t claimCount,
    const std::string& matcherSource) {
  std::vector<std::string> violations = {};
  for (const auto& processor : scanned) {
    const ProcessorClaim* claim = nullptr;
    for (size_t i = 0; i < claimCount; ++i) {
      if (claims[i].className == processor.className) {
        claim = &claims[i];
        break;
      }
    }
    if (claim == nullptr) {
      violations.push_back("unclaimed processor class: " + processor.className + " (" +
                           processor.filePath +
                           ") — add a matcher rule, a lowerToAOT, or a "
                           "documented runtime-by-design entry");
      continue;
    }
    if ((claim->claims & (kMatcher | kStructural)) != 0) {
      bool referenced =
          matcherSource.find(claim->nameString) != std::string::npos ||
          matcherSource.find(std::string(claim->className) + "::") != std::string::npos;
      if (!referenced) {
        violations.push_back("matcher reference missing for: " + processor.className +
                             " (expected \"" + claim->nameString + "\" or \"" + claim->className +
                             "::\" in PermutationMatcher.cpp)");
      }
    }
    if ((claim->claims & kLowering) != 0 && !processor.hasLowerToAOT) {
      violations.push_back("lowerToAOT missing for: " + processor.className);
    }
    if (claim->claims == 0 && std::string(claim->note).empty()) {
      violations.push_back("runtime-by-design entry without a reason: " + processor.className);
    }
  }
  for (size_t i = 0; i < claimCount; ++i) {
    bool found = false;
    for (const auto& processor : scanned) {
      if (processor.className == claims[i].className) {
        found = true;
        break;
      }
    }
    if (!found) {
      violations.push_back("stale claim entry (no such processor class): " +
                           std::string(claims[i].className));
    }
  }
  return violations;
}

TGFX_TEST(AOTCoverageGateTest, AllProcessorsAreClaimed) {
  auto scanned = ScanProcessorHeaders();
  ASSERT_FALSE(scanned.empty());
  auto matcherSource = ReadTextFile(ProjectPath::Absolute("src/gpu/PermutationMatcher.cpp"));
  ASSERT_FALSE(matcherSource.empty());
  auto violations =
      EvaluateProcessorClaims(scanned, ProcessorClaims,
                              sizeof(ProcessorClaims) / sizeof(ProcessorClaims[0]), matcherSource);
  for (const auto& violation : violations) {
    printf("[CoverageGate] %s\n", violation.c_str());
  }
  EXPECT_TRUE(violations.empty());
}

// Verifies the gate itself rejects the violation shapes it exists to catch, using synthetic
// scan/manifest inputs: a new unclaimed class, a stale manifest entry, a claimed name the
// matcher never references, and a runtime entry without a reason.
TGFX_TEST(AOTCoverageGateTest, GateRejectsSyntheticViolations) {
  std::vector<ScannedProcessor> scanned = {};
  ScannedProcessor good = {};
  good.className = "GoodProcessor";
  good.hasLowerToAOT = true;
  scanned.push_back(good);
  ScannedProcessor orphan = {};
  orphan.className = "BrandNewEffectProcessor";
  scanned.push_back(orphan);
  ScannedProcessor missingReference = {};
  missingReference.className = "GoodProcessorMissingReference";
  scanned.push_back(missingReference);
  ScannedProcessor reasonless = {};
  reasonless.className = "ReasonlessRuntimeProcessor";
  scanned.push_back(reasonless);

  const ProcessorClaim claims[] = {
      {"GoodProcessor", "GoodProcessor", kLowering, ""},
      {"GoodProcessorMissingReference", "GoodProcessorMissingReference", kMatcher, ""},
      {"RemovedProcessor", "RemovedProcessor", kMatcher, ""},
      {"ReasonlessRuntimeProcessor", "ReasonlessRuntimeProcessor", 0, ""},
  };
  // The matcher source deliberately lacks every synthetic name.
  auto violations = EvaluateProcessorClaims(scanned, claims, 4, "// empty matcher");
  EXPECT_EQ(violations.size(), 4u);
  bool sawUnclaimed = false;
  bool sawStale = false;
  bool sawMissingReference = false;
  bool sawReasonless = false;
  for (const auto& violation : violations) {
    if (violation.find("unclaimed processor class: BrandNewEffectProcessor") != std::string::npos) {
      sawUnclaimed = true;
    }
    if (violation.find("stale claim entry (no such processor class): RemovedProcessor") !=
        std::string::npos) {
      sawStale = true;
    }
    if (violation.find("matcher reference missing for: GoodProcessorMissingReference") !=
        std::string::npos) {
      sawMissingReference = true;
    }
    if (violation.find("runtime-by-design entry without a reason") != std::string::npos) {
      sawReasonless = true;
    }
  }
  EXPECT_TRUE(sawUnclaimed);
  EXPECT_TRUE(sawStale);
  EXPECT_TRUE(sawMissingReference);
  EXPECT_TRUE(sawReasonless);
}

// Defense-in-depth from WP4: every two-processor xfermode construction site outside the
// XfermodeFragmentProcessor implementation itself must live in a translation unit that wraps its
// blend children with EnsureSimpleBlendChild — the pre-draw materialization that keeps
// over-sampler-budget blend trees off the runtime route. A new construction site that skips the
// wrapper would reintroduce whole-tree fallbacks for nested blends.
// Defense-in-depth from WP4: every two-processor xfermode construction site outside the
// XfermodeFragmentProcessor implementation itself must live in a translation unit that wraps its
// blend children with EnsureSimpleBlendChild — the pre-draw materialization that keeps
// over-sampler-budget blend trees off the runtime route. A new construction site that skips the
// wrapper would reintroduce whole-tree fallbacks for nested blends. A site whose operands are
// structurally capped below the fused sampler budget may be exempt instead; the exemption must
// name the cap so it stays honest under review.
struct BlendSiteExemption {
  const char* fileName;
  const char* reason;
};

const BlendSiteExemption BlendSiteExemptions[] = {
    {"ColorImageFilter.cpp",
     "both operands are single-texture-leaf trees (an image sampled through a unary color-filter "
     "chain, and a plain image sample), so the pair can never exceed the four-sampler budget"},
};

TGFX_TEST(AOTCoverageGateTest, BlendConstructionSitesStayFlattened) {
  auto srcRoot = std::filesystem::path(ProjectPath::Absolute("src"));
  ASSERT_TRUE(std::filesystem::exists(srcRoot));
  int checkedSites = 0;
  std::vector<std::string> violations = {};
  std::error_code error = {};
  for (auto it = std::filesystem::recursive_directory_iterator(srcRoot, error);
       it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
    if (error || !it->is_regular_file() || it->path().extension() != ".cpp") {
      continue;
    }
    auto fileName = it->path().filename().string();
    auto text = ReadTextFile(it->path().string());
    if (text.find("XfermodeFragmentProcessor::MakeFromTwoProcessors") == std::string::npos) {
      continue;
    }
    // The MakeFromTwoProcessors implementation files themselves (the function definition and the
    // single-child convenience forms delegating to it) are not construction sites.
    if (fileName == "XfermodeFragmentProcessor.cpp" ||
        fileName == "GLSLXfermodeFragmentProcessor.cpp") {
      continue;
    }
    ++checkedSites;
    if (text.find("EnsureSimpleBlendChild") != std::string::npos) {
      continue;
    }
    bool exempt = false;
    for (const auto& exemption : BlendSiteExemptions) {
      if (fileName == exemption.fileName) {
        exempt = true;
        break;
      }
    }
    if (!exempt) {
      violations.push_back(
          "xfermode two-processor construction without EnsureSimpleBlendChild (and without a "
          "documented budget exemption): " +
          it->path().string());
    }
  }
  for (const auto& violation : violations) {
    printf("[CoverageGate] %s\n", violation.c_str());
  }
  EXPECT_TRUE(violations.empty());
  EXPECT_GE(checkedSites, 6);
}

TGFX_TEST(AOTCoverageGateTest, ProductionMetricsPreserveUnmarkedFailures) {
  EXPECT_TRUE(EvaluateAOTCoverageGate(0, 0, 0, 0, 0, 0).passed());
  EXPECT_TRUE(EvaluateAOTCoverageGate(1, 1, 0, 0, 2, 2).passed());
  auto mixed = EvaluateAOTCoverageGate(2, 1, 0, 0, 2, 1);
  EXPECT_EQ(mixed.noMatchingRule, 1u);
  EXPECT_EQ(mixed.runtimeCompiles, 1u);
  EXPECT_FALSE(mixed.passed());
  EXPECT_FALSE(EvaluateAOTCoverageGate(1, 0, 0, 0, 0, 0).passed());
  EXPECT_FALSE(EvaluateAOTCoverageGate(0, 0, 1, 0, 0, 0).passed());
  EXPECT_FALSE(EvaluateAOTCoverageGate(0, 1, 0, 0, 0, 0).consistent);
  EXPECT_FALSE(EvaluateAOTCoverageGate(0, 0, 0, 1, 0, 0).passed());
  // By-design and environment exclusions pair with their fallback compilations: the strict
  // counts drop, the exclusion counts surface, and an unpaired miss still fails.
  auto byDesign = EvaluateAOTCoverageGate(2, 0, 2, 0, 3, 1);
  EXPECT_TRUE(byDesign.passed());
  EXPECT_EQ(byDesign.excludedByDesign, 2u);
  EXPECT_EQ(byDesign.excludedEnvironment, 0u);
  auto environment = EvaluateAOTCoverageGate(0, 0, 0, 3, 3, 0);
  EXPECT_TRUE(environment.passed());
  EXPECT_EQ(environment.excludedEnvironment, 3u);
  // An exclusion may never hide a miss it cannot pair with: subtracting more builder creations
  // than exist flips consistency instead of clamping the count to zero.
  EXPECT_FALSE(EvaluateAOTCoverageGate(2, 0, 2, 0, 1, 0).consistent);
  EXPECT_FALSE(EvaluateAOTCoverageGate(0, 0, 0, 0, 0, 1).passed());

  GlobalCache programs(nullptr);
  ProgramProvenance provenance = {};
  BytesKey excludedKey;
  excludedKey.write(1u);
  programs.addProgram(excludedKey, nullptr, true);
  EXPECT_EQ(programs.programStats().excludedProgramBuilderCreations, 0u);
  auto excludedProgram = std::make_shared<Program>(nullptr, nullptr, nullptr, provenance);
  programs.addProgram(excludedKey, excludedProgram, true);
  EXPECT_EQ(programs.programStats().programBuilderCreations, 1u);
  EXPECT_EQ(programs.programStats().excludedProgramBuilderCreations, 1u);
  EXPECT_EQ(programs.findProgram(excludedKey), excludedProgram);
  EXPECT_EQ(programs.programStats().excludedProgramBuilderCreations, 1u);

  BytesKey ordinaryKey;
  ordinaryKey.write(2u);
  programs.addProgram(ordinaryKey,
                      std::make_shared<Program>(nullptr, nullptr, nullptr, provenance));
  const auto& stats = programs.programStats();
  EXPECT_EQ(stats.programBuilderCreations, 2u);
  EXPECT_EQ(stats.excludedProgramBuilderCreations, 1u);
  auto gate = EvaluateAOTCoverageGate(1, 1, 0, 0, stats.programBuilderCreations,
                                      stats.excludedProgramBuilderCreations);
  EXPECT_EQ(gate.runtimeCompiles, 1u);
  EXPECT_FALSE(gate.passed());

  programs.setProgramStatsPaused(true);
  BytesKey pausedKey;
  pausedKey.write(3u);
  programs.addProgram(pausedKey, std::make_shared<Program>(nullptr, nullptr, nullptr, provenance),
                      true);
  EXPECT_EQ(stats.programBuilderCreations, 2u);
  EXPECT_EQ(stats.excludedProgramBuilderCreations, 1u);
  programs.resetProgramStats();
  EXPECT_EQ(stats.programBuilderCreations, 0u);
  EXPECT_EQ(stats.excludedProgramBuilderCreations, 0u);
}

TGFX_TEST(AOTCoverageGateTest, DeliberateScopeRestoresNestedState) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto* cache = context->precompiledShaderCache();
  ASSERT_FALSE(cache->deliberateMissMarking());
  auto markAndReturn = [&] {
    ScopedAOTDeliberateMiss outer(context);
    EXPECT_TRUE(cache->deliberateMissMarking());
    {
      ScopedAOTDeliberateMiss inner(context);
      EXPECT_TRUE(cache->deliberateMissMarking());
    }
    EXPECT_TRUE(cache->deliberateMissMarking());
    return;
  };
  markAndReturn();
  EXPECT_FALSE(cache->deliberateMissMarking());
  ScopedAOTDeliberateMiss inactive(context, false);
  EXPECT_FALSE(cache->deliberateMissMarking());
}

// Negative control for the blocking summary gate (audit F10): when TGFX_AOT_TEST_INJECT_MISS is
// set, this test renders one filtered draw without the bundle and without the stats pause, so
// its runtime compilation lands in the production metrics. With
// TGFX_AOT_COVERAGE_GATE=blocking also set, the suite-level summary must then fail the process
// (exit code 1, BLOCKING line). Without the variables this test is inert, which is how the
// suite normally runs.
TGFX_TEST(AOTCoverageGateTest, InjectedMissFailsTheBlockingGate) {
  auto injection = std::getenv("TGFX_AOT_TEST_INJECT_MISS");
  if (injection == nullptr) {
    GTEST_SKIP() << "negative control only runs when TGFX_AOT_TEST_INJECT_MISS is set";
    return;
  }
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto* cache = context->precompiledShaderCache();
  if (std::string(injection) == "mixed-first" || std::string(injection) == "mixed-last") {
    bool markedFirst = std::string(injection) == "mixed-first";
    cache->setDiagnosticRecordingEnabled(true);
    PrecompiledFallbackRecord record = {};
    record.effectSignature = "InjectedOrdinaryStructure";
    record.pipelineSignature = record.effectSignature;
    for (bool marked : {markedFirst, !markedFirst}) {
      ScopedAOTDeliberateMiss deliberate(context, marked);
      cache->recordAOTStage(PrecompiledAOTStage::Attempt);
      cache->recordAOTStage(PrecompiledAOTStage::CacheAvailable);
      cache->recordArtifactMiss(PrecompiledFallbackReason::NoMatchingRule, record);
    }
    EXPECT_EQ(cache->fallbackCount(PrecompiledFallbackReason::NoMatchingRule), 2u);
    EXPECT_EQ(context->globalCache()->programStats().programBuilderCreations, 0u);
    return;
  }
  cache->unload();
  cache->setDiagnosticRecordingEnabled(true);
  auto surface = Surface::Make(context, 64, 64);
  ASSERT_NE(surface, nullptr);
  std::array<float, 20> swapRedBlue = {0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0};
  Paint paint = {};
  paint.setColorFilter(ColorFilter::Matrix(swapRedBlue));
  surface->getCanvas()->drawRect(Rect::MakeWH(64, 64), paint);
  context->flushAndSubmit(true);
}

}  // namespace
}  // namespace tgfx

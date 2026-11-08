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
// A class is claimed when at least one of the following holds, and every category is proven by a
// structural fact of the codebase — never by a text occurrence in the matcher, which moves
// whenever matching is restructured:
//   - GeometryClassified: DescribeGeometryProcessor (the one geometry classifier) references it;
//   - XferClassified: ClassifyXferForMatch (the one xfer classifier) references it;
//   - Lowering: the class declares lowerToAOT (verified in its header), so the FP tree can
//     become IR;
//   - ParentLowering: it is consumed by its parent's lowerToAOT (gradient layouts and
//     colorizers lower inside ClampedGradientEffect), verified by the parent declaring
//     lowerToAOT and the parent's source referencing the class;
//   - none of the above (runtime by design), which requires a documented reason.
// The classifier bodies are located by their definitions; a renamed or moved classifier fails
// the gate loudly instead of silently emptying the evidence set. AOTClosureVerifier covers a
// different axis (registered shaders' reachable sets fit inside the published bundles); this gate
// covers the processor-type axis and the two checks coexist.

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>
#include "base/TGFXTest.h"
#include "gpu/ContextInternal.h"
#include "gpu/GlobalCache.h"
#include "gpu/ProgramWriter.h"
#include "gpu/aot/bundle/PrecompiledShaderCache.h"
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

constexpr uint32_t GeometryClassified = 1;
constexpr uint32_t XferClassified = 2;
constexpr uint32_t Lowering = 4;
constexpr uint32_t ParentLowering = 8;

struct ProcessorClaim {
  const char* className = "";
  uint32_t claims = 0;
  const char* note = "";
  // ParentLowering only: the processor class whose lowerToAOT consumes this one.
  const char* parent = "";
};

// The claim manifest. Keep in sync with the processor headers: adding a class without a claim
// entry (or a reason) fails the gate, and removing a class leaves a stale entry that also fails.
const ProcessorClaim ProcessorClaims[] = {
    // Geometry processors: each is classified by DescribeGeometryProcessor.
    {"AtlasTextGeometryProcessor", GeometryClassified, ""},
    {"ComplexEllipseGeometryProcessor", GeometryClassified, ""},
    {"ComplexNonAARRectGeometryProcessor", GeometryClassified, ""},
    {"DefaultGeometryProcessor", GeometryClassified, ""},
    {"EllipseGeometryProcessor", GeometryClassified, ""},
    {"HairlineLineGeometryProcessor", GeometryClassified, ""},
    {"HairlineQuadGeometryProcessor", GeometryClassified, ""},
    {"MeshGeometryProcessor", GeometryClassified, ""},
    {"NonAARRectGeometryProcessor", GeometryClassified, ""},
    {"QuadPerEdgeAAGeometryProcessor", GeometryClassified, ""},
    {"RoundStrokeRectGeometryProcessor", GeometryClassified, ""},
    {"ShapeInstancedGeometryProcessor", GeometryClassified, ""},
    // Xfer processors: ClassifyXferForMatch names both (the empty one through its singleton
    // identity).
    {"EmptyXferProcessor", XferClassified, ""},
    {"PorterDuffXferProcessor", XferClassified, ""},
    // Fragment processors carrying an AOT lowering: lowerToAOT is the structural fact, and the
    // families read the resulting IR instead of walking the processor tree.
    {"AlphaThresholdFragmentProcessor", Lowering, ""},
    {"ClampedGradientEffect", Lowering, ""},
    {"ColorMatrixFragmentProcessor", Lowering, ""},
    {"ColorSpaceXformEffect", Lowering, ""},
    {"ComposeFragmentProcessor", Lowering, "structural container: lowers by chaining its children"},
    {"ConstColorProcessor", Lowering, "served as a chain slot through the lowering"},
    {"DeviceSpaceTextureEffect", Lowering, ""},
    {"LumaFragmentProcessor", Lowering, ""},
    {"PerlinNoiseFragmentProcessor", Lowering, ""},
    {"RectEffect", Lowering, ""},
    {"RRectEffect", Lowering, ""},
    {"TextureEffect", Lowering, ""},
    {"TiledTextureEffect", Lowering, ""},
    {"XfermodeFragmentProcessor", Lowering,
     "the SrcIn local-mask coverage form is served; two-processor blends lower"},
    // Gradient parts: ClampedGradientEffect::lowerToAOT reads each layout and colorizer into the
    // gradient node's parameters, so the parent's lowering is their claim.
    {"ConicGradientLayout", ParentLowering, "", "ClampedGradientEffect"},
    {"DiamondGradientLayout", ParentLowering, "", "ClampedGradientEffect"},
    {"LinearGradientLayout", ParentLowering, "", "ClampedGradientEffect"},
    {"RadialGradientLayout", ParentLowering, "", "ClampedGradientEffect"},
    {"SingleIntervalGradientColorizer", ParentLowering, "", "ClampedGradientEffect"},
    {"DualIntervalGradientColorizer", ParentLowering, "", "ClampedGradientEffect"},
    {"TextureGradientColorizer", ParentLowering, "", "ClampedGradientEffect"},
    {"UnrolledBinaryGradientColorizer", ParentLowering, "", "ClampedGradientEffect"},
    // Glass geometry children consumed inside GlassRefractionFragmentProcessor::lowerToAOT —
    // the parent's opaque node carries their kind as a variant dimension.
    {"GlassSDFGeometryFragmentProcessor", ParentLowering, "", "GlassRefractionFragmentProcessor"},
    {"GlassUDFGeometryFragmentProcessor", ParentLowering, "", "GlassRefractionFragmentProcessor"},
    // Effect processors served by dedicated families: they lower into the IR's opaque-kernel
    // node, and the family matchers read that node instead of walking the processor tree.
    {"GaussianBlur1DFragmentProcessor", Lowering, ""},
    {"GlassRefractionFragmentProcessor", Lowering, ""},
    {"GlassUDFTentBlurFragmentProcessor", Lowering, ""},
};

struct ScannedProcessor {
  std::string className;
  std::string filePath;
  bool hasLowerToAOT = false;
  // The implementation source next to the header (X.h -> X.cpp), read for parent-lowering claims.
  std::string sourceText;
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

// Returns the body of the function whose definition begins with `definitionHead` (the signature
// up to and including the opening brace), or an empty string when the definition is absent. The
// classifier bodies carry no braces inside strings or comments, so plain counting suffices; a
// missing definition is itself a gate failure, never a silent pass.
static std::string ExtractFunctionBody(const std::string& source,
                                       const std::string& definitionHead) {
  auto start = source.find(definitionHead);
  if (start == std::string::npos) {
    return {};
  }
  auto open = start + definitionHead.size() - 1;
  int depth = 0;
  for (size_t index = open; index < source.size(); ++index) {
    if (source[index] == '{') {
      ++depth;
    } else if (source[index] == '}') {
      --depth;
      if (depth == 0) {
        return source.substr(open, index - open + 1);
      }
    }
  }
  return {};
}

// Returns the name of the class declaration (`class X ... {`) that most closely precedes the
// given position, or an empty string when none exists. The line-anchored pattern skips forward
// declarations (they carry a semicolon before any brace) and `friend class` mentions (they have
// a `friend ` prefix).
static std::string OwningClassBefore(const std::string& text, size_t position) {
  // The line anchor is spelled as an explicit "(?:^|\n)" instead of the multiline flag: MSVC's
  // <regex> has never implemented std::regex_constants::multiline, so the flag is unavailable
  // there. Without multiline "^" matches only the start of the string, and the alternative covers
  // every other line start, which is the same language.
  static const std::regex classPattern = std::regex("(?:^|\\n)[ \\t]*class\\s+(\\w+)[^{;]*\\{");
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
    auto sourcePath = path;
    sourcePath.replace_extension(".cpp");
    processor.sourceText = ReadTextFile(sourcePath.string());
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

// The pure gate check, factored out so synthetic violations can verify the gate itself. A claim
// is proven by a structural fact, never by a text occurrence in the matcher: a geometry claim by
// the class appearing in DescribeGeometryProcessor's body, an xfer claim by ClassifyXferForMatch's
// body, a lowering claim by the header declaring lowerToAOT, a parent-lowering claim by the
// parent's lowerToAOT source consuming the class, and a runtime-by-design entry by its documented
// reason.
static std::vector<std::string> EvaluateProcessorClaims(
    const std::vector<ScannedProcessor>& scanned, const ProcessorClaim* claims, size_t claimCount,
    const std::string& geometryClassifierBody, const std::string& xferClassifierBody) {
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
                           ") — add a classifier entry, a lowerToAOT, or a documented "
                           "runtime-by-design entry");
      continue;
    }
    if ((claim->claims & GeometryClassified) != 0 &&
        geometryClassifierBody.find(processor.className) == std::string::npos) {
      violations.push_back("geometry classifier does not reference: " + processor.className +
                           " (expected IsProcessor<" + processor.className +
                           "> in DescribeGeometryProcessor)");
    }
    if ((claim->claims & XferClassified) != 0 &&
        xferClassifierBody.find(processor.className) == std::string::npos) {
      violations.push_back("xfer classifier does not reference: " + processor.className +
                           " (expected " + processor.className + " in ClassifyXferForMatch)");
    }
    if ((claim->claims & Lowering) != 0 && !processor.hasLowerToAOT) {
      violations.push_back("lowerToAOT missing for: " + processor.className);
    }
    if ((claim->claims & ParentLowering) != 0) {
      const ScannedProcessor* parent = nullptr;
      for (const auto& candidate : scanned) {
        if (candidate.className == claim->parent) {
          parent = &candidate;
          break;
        }
      }
      if (parent == nullptr || !parent->hasLowerToAOT ||
          parent->sourceText.find(processor.className) == std::string::npos) {
        violations.push_back("parent lowering does not consume: " + processor.className +
                             " (expected " + claim->parent +
                             "::lowerToAOT and a reference in its source)");
      }
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
  auto matcherSource =
      ReadTextFile(ProjectPath::Absolute("src/gpu/aot/select/PermutationMatcher.cpp"));
  ASSERT_FALSE(matcherSource.empty());
  // The evidence is the classifiers' own bodies, not the file around them: a renamed or moved
  // classifier fails here loudly instead of silently emptying the evidence set.
  auto geometryClassifier = ExtractFunctionBody(
      matcherSource, "DescribeGeometryProcessor(const GeometryProcessor* gp) {");
  auto xferClassifier =
      ExtractFunctionBody(matcherSource, "ClassifyXferForMatch(const XferProcessor* xp) {");
  ASSERT_FALSE(geometryClassifier.empty());
  ASSERT_FALSE(xferClassifier.empty());
  auto violations = EvaluateProcessorClaims(scanned, ProcessorClaims,
                                            sizeof(ProcessorClaims) / sizeof(ProcessorClaims[0]),
                                            geometryClassifier, xferClassifier);
  for (const auto& violation : violations) {
    printf("[CoverageGate] %s\n", violation.c_str());
  }
  EXPECT_TRUE(violations.empty());
}

// Verifies the gate itself rejects the violation shapes it exists to catch, using synthetic
// scan/manifest inputs: a new unclaimed class, a stale manifest entry, a classified claim the
// classifier never references, and a runtime entry without a reason.
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
  ScannedProcessor parent = {};
  parent.className = "ParentProcessor";
  parent.hasLowerToAOT = true;
  parent.sourceText = "// reads ConsumedPart";
  scanned.push_back(parent);
  ScannedProcessor consumed = {};
  consumed.className = "ConsumedPart";
  scanned.push_back(consumed);
  ScannedProcessor forgotten = {};
  forgotten.className = "ForgottenPart";
  scanned.push_back(forgotten);

  const ProcessorClaim claims[] = {
      {"GoodProcessor", Lowering, ""},
      {"GoodProcessorMissingReference", GeometryClassified, ""},
      {"RemovedProcessor", GeometryClassified, ""},
      {"ReasonlessRuntimeProcessor", 0, ""},
      {"ParentProcessor", Lowering, ""},
      {"ConsumedPart", ParentLowering, "", "ParentProcessor"},
      {"ForgottenPart", ParentLowering, "", "ParentProcessor"},
  };
  // The classifier bodies deliberately lack every synthetic name.
  auto violations =
      EvaluateProcessorClaims(scanned, claims, 7, "// empty geometry", "// empty xfer");
  EXPECT_EQ(violations.size(), 5u);
  bool sawForgottenPart = false;
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
    if (violation.find("geometry classifier does not reference: GoodProcessorMissingReference") !=
        std::string::npos) {
      sawMissingReference = true;
    }
    if (violation.find("runtime-by-design entry without a reason") != std::string::npos) {
      sawReasonless = true;
    }
    if (violation.find("parent lowering does not consume: ForgottenPart") != std::string::npos) {
      sawForgottenPart = true;
    }
  }
  EXPECT_TRUE(sawForgottenPart);
  EXPECT_TRUE(sawUnclaimed);
  EXPECT_TRUE(sawStale);
  EXPECT_TRUE(sawMissingReference);
  EXPECT_TRUE(sawReasonless);
}

TGFX_TEST(AOTCoverageGateTest, ProductionMetricsPreserveUnmarkedFailures) {
  EXPECT_TRUE(EvaluateAOTCoverageGate(0, 0, 0).passed());
  EXPECT_TRUE(EvaluateAOTCoverageGate(1, 1, 0).passed());
  auto mixed = EvaluateAOTCoverageGate(2, 1, 0);
  EXPECT_EQ(mixed.noMatchingRule, 1u);
  EXPECT_FALSE(mixed.passed());
  EXPECT_FALSE(EvaluateAOTCoverageGate(1, 0, 0).passed());
  EXPECT_FALSE(EvaluateAOTCoverageGate(0, 1, 0).consistent);
  // An environment failure has no paired runtime compilation to subtract (runtime compilation
  // no longer exists): it can only surface, and it breaks consistency.
  auto environment = EvaluateAOTCoverageGate(0, 0, 3);
  EXPECT_EQ(environment.excludedEnvironment, 3u);
  EXPECT_FALSE(environment.consistent);
  EXPECT_FALSE(EvaluateAOTCoverageGate(0, 0, 0, 1).passed());
  // A dropped draw fails the gate even with no miss: it rendered nothing, whatever the reason
  // its program lookup failed.
  auto dropped = EvaluateAOTCoverageGate(0, 0, 0);
  dropped.droppedDraws = 1;
  EXPECT_FALSE(dropped.passed());
  // A run with no miss is only evidence of coverage when it also served a precompiled program:
  // drawing nothing (or only excluded draws) is vacuous, not met.
  EXPECT_FALSE(AOTCoverageGateMet(EvaluateAOTCoverageGate(0, 0, 0), 0));
  EXPECT_TRUE(AOTCoverageGateMet(EvaluateAOTCoverageGate(0, 0, 0), 1));
  EXPECT_FALSE(AOTCoverageGateMet(EvaluateAOTCoverageGate(0, 0, 0, 1), 5));

  GlobalCache programs(nullptr);
  BytesKey firstKey;
  firstKey.write(1u);
  programs.addProgram(firstKey, nullptr);
  EXPECT_EQ(programs.programStats().precompiledArtifactCreations, 0u);
  auto firstProgram =
      std::make_shared<Program>(nullptr, nullptr, nullptr, std::unique_ptr<ProgramWriter>());
  programs.addProgram(firstKey, firstProgram);
  EXPECT_EQ(programs.programStats().precompiledArtifactCreations, 1u);
  EXPECT_EQ(programs.findProgram(firstKey), firstProgram);

  BytesKey secondKey;
  secondKey.write(2u);
  programs.addProgram(secondKey, std::make_shared<Program>(nullptr, nullptr, nullptr,
                                                           std::unique_ptr<ProgramWriter>()));
  const auto& stats = programs.programStats();
  EXPECT_EQ(stats.precompiledArtifactCreations, 2u);

  programs.setProgramStatsPaused(true);
  BytesKey pausedKey;
  pausedKey.write(3u);
  programs.addProgram(pausedKey, std::make_shared<Program>(nullptr, nullptr, nullptr,
                                                           std::unique_ptr<ProgramWriter>()));
  EXPECT_EQ(stats.precompiledArtifactCreations, 2u);
  programs.resetProgramStats();
  EXPECT_EQ(stats.precompiledArtifactCreations, 0u);
}

// A listed gap leaves the strict residual but never disappears from the report, and a table entry
// that describes nothing fails the gate: the exception list stays honest in both directions.
TGFX_TEST(AOTCoverageGateTest, KnownGapsAreSurfacedNotHidden) {
  auto listed = EvaluateAOTCoverageGate(3, 0, 0, 3);
  EXPECT_TRUE(listed.passed());
  EXPECT_EQ(listed.noMatchingRule, 0u);
  EXPECT_EQ(listed.excludedKnownGaps, 3u);
  // The exclusion may never subtract more than the run recorded.
  EXPECT_FALSE(EvaluateAOTCoverageGate(2, 0, 0, 3).consistent);
  // An unlisted miss stays a failure even when the table covers other shapes.
  auto unlisted = EvaluateAOTCoverageGate(4, 0, 0, 3);
  EXPECT_EQ(unlisted.noMatchingRule, 1u);
  EXPECT_FALSE(unlisted.passed());
  // A stale entry is a cleanup debt, not a silent pass.
  auto stale = EvaluateAOTCoverageGate(0, 0, 0, 0);
  stale.staleKnownGaps = 1;
  EXPECT_FALSE(stale.passed());
  EXPECT_FALSE(AOTCoverageGateMet(stale, 1));
}

TGFX_TEST(AOTCoverageGateTest, DeliberateScopeRestoresNestedState) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_NE(context, nullptr);
  auto* cache = ContextCacheAccess::Cache(context);
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
}  // namespace
}  // namespace tgfx

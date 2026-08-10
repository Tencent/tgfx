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

#include "ShaderAOTTestReporter.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include "gpu/AOTEffect.h"
#include "gpu/EmbeddedShaderBundles.h"
#include "gpu/GlobalCache.h"
#include "gpu/PrecompiledShaderCache.h"
#include "gtest/gtest.h"
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunknown-warning-option"
#pragma clang diagnostic ignored "-Wdeprecated-literal-operator"
#include "nlohmann/json.hpp"
#pragma clang diagnostic pop
#include "utils/DevicePool.h"
#include "utils/ProjectPath.h"

namespace tgfx {

#ifndef TGFX_BACKEND_NAME
#define TGFX_BACKEND_NAME "unknown"
#endif

static constexpr size_t FALLBACK_REASON_COUNT =
    static_cast<size_t>(PrecompiledFallbackReason::Count);
static constexpr size_t AOT_STAGE_COUNT = static_cast<size_t>(PrecompiledAOTStage::Count);
static constexpr size_t OFFSCREEN_FILL_SOURCE_COUNT =
    static_cast<size_t>(OffscreenFillSource::Count);

struct ShaderAOTTestResult {
  std::string testName;
  bool passed = false;
  bool skipped = false;
  bool contextAvailable = false;
  bool bundleLoadedAtStart = false;
  bool bundleLoadedAtEnd = false;
  std::string profileTag;
  ProgramCacheStats programStats = {};
  uint32_t artifactHits = 0;
  uint32_t artifactMisses = 0;
  std::array<uint32_t, AOT_STAGE_COUNT> aotStageCounts = {};
  std::array<uint32_t, FALLBACK_REASON_COUNT> fallbackCounts = {};
  std::vector<PrecompiledHitRecord> hitRecords = {};
  std::vector<PrecompiledFallbackRecord> fallbackRecords = {};
  std::vector<JITProgramRecord> jitProgramRecords = {};
  AOTDrawStats drawStats = {};
  std::array<OffscreenFillStats, OFFSCREEN_FILL_SOURCE_COUNT> offscreenFillStats = {};
  std::vector<OffscreenFillCorrelation> offscreenFillCorrelations = {};
};

struct ShaderAOTSummary {
  ProgramCacheStats programStats = {};
  uint64_t artifactHits = 0;
  uint64_t artifactMisses = 0;
  std::array<uint64_t, AOT_STAGE_COUNT> aotStageCounts = {};
  std::array<uint64_t, FALLBACK_REASON_COUNT> fallbackCounts = {};
  AOTDrawStats drawStats = {};
  std::array<OffscreenFillStats, OFFSCREEN_FILL_SOURCE_COUNT> offscreenFillStats = {};
  std::unordered_map<std::string, OffscreenFillCorrelation> offscreenFillCorrelations = {};
};

static void AddOffscreenFillStats(OffscreenFillStats* target, const OffscreenFillStats& source) {
  target->calls += source.calls;
  target->coordOffsetNonZero += source.coordOffsetNonZero;
  target->lowerSucceeded += source.lowerSucceeded;
  target->validateSucceeded += source.validateSucceeded;
  target->decomposeSucceeded += source.decomposeSucceeded;
  target->canExecute += source.canExecute;
  target->pointwiseChainPlans += source.pointwiseChainPlans;
  target->pointwiseTailPlans += source.pointwiseTailPlans;
  target->multiPassPlans += source.multiPassPlans;
  target->precompiledPrograms += source.precompiledPrograms;
  target->programBuilderPrograms += source.programBuilderPrograms;
  target->precompiledCanExecute += source.precompiledCanExecute;
  target->programBuilderCanExecute += source.programBuilderCanExecute;
  for (const auto& [name, count] : source.lowerBlockers) {
    target->lowerBlockers[name] += count;
  }
  for (const auto& [name, count] : source.topLevelProcessors) {
    target->topLevelProcessors[name] += count;
  }
}

static void AddOffscreenFillCorrelation(
    std::unordered_map<std::string, OffscreenFillCorrelation>* target,
    const OffscreenFillCorrelation& source) {
  auto key = std::string(OffscreenFillSourceName(source.source)) + "|" + source.topLevelProcessor +
             "|" + source.kernelSignature;
  auto& value = (*target)[key];
  value.source = source.source;
  value.topLevelProcessor = source.topLevelProcessor;
  value.kernelSignature = source.kernelSignature;
  value.calls += source.calls;
  value.canExecute += source.canExecute;
  value.precompiledPrograms += source.precompiledPrograms;
  value.programBuilderPrograms += source.programBuilderPrograms;
  value.precompiledCanExecute += source.precompiledCanExecute;
  value.programBuilderCanExecute += source.programBuilderCanExecute;
}

static void AddDrawStats(AOTDrawStats* target, const AOTDrawStats& source) {
  target->draws += source.draws;
  target->completeAOTDraws += source.completeAOTDraws;
  target->atomicFallbacks += source.atomicFallbacks;
  target->kernelInvocations += source.kernelInvocations;
  target->offscreenTargets += source.offscreenTargets;
  target->materializedEdges += source.materializedEdges;
  target->renderTargetSwitches += source.renderTargetSwitches;
  target->intermediateReadBytes += source.intermediateReadBytes;
  target->intermediateWriteBytes += source.intermediateWriteBytes;
  target->peakTemporaryBytes = std::max(target->peakTemporaryBytes, source.peakTemporaryBytes);
}

static nlohmann::json OffscreenFillStatsToJSON(const OffscreenFillStats& stats) {
  return {{"calls", stats.calls},
          {"coordOffsetNonZero", stats.coordOffsetNonZero},
          {"lowerSucceeded", stats.lowerSucceeded},
          {"validateSucceeded", stats.validateSucceeded},
          {"decomposeSucceeded", stats.decomposeSucceeded},
          {"canExecute", stats.canExecute},
          {"pointwiseChainPlans", stats.pointwiseChainPlans},
          {"pointwiseTailPlans", stats.pointwiseTailPlans},
          {"multiPassPlans", stats.multiPassPlans},
          {"precompiledPrograms", stats.precompiledPrograms},
          {"programBuilderPrograms", stats.programBuilderPrograms},
          {"precompiledCanExecute", stats.precompiledCanExecute},
          {"programBuilderCanExecute", stats.programBuilderCanExecute},
          {"lowerBlockers", stats.lowerBlockers},
          {"topLevelProcessors", stats.topLevelProcessors}};
}

static nlohmann::json DrawStatsToJSON(const AOTDrawStats& stats) {
  return {{"draws", stats.draws},
          {"completeAOTDraws", stats.completeAOTDraws},
          {"atomicFallbacks", stats.atomicFallbacks},
          {"kernelInvocations", stats.kernelInvocations},
          {"offscreenTargets", stats.offscreenTargets},
          {"materializedEdges", stats.materializedEdges},
          {"renderTargetSwitches", stats.renderTargetSwitches},
          {"intermediateReadBytes", stats.intermediateReadBytes},
          {"intermediateWriteBytes", stats.intermediateWriteBytes},
          {"peakTemporaryBytes", stats.peakTemporaryBytes}};
}

struct AggregatedShader {
  std::string shaderName;
  uint64_t count = 0;
  std::set<std::string> tests = {};
};

struct AggregatedHit {
  PrecompiledHitRecord record = {};
  uint64_t count = 0;
  std::set<std::string> tests = {};
};

struct AggregatedFallback {
  PrecompiledFallbackRecord record = {};
  uint64_t count = 0;
  std::set<std::string> tests = {};
};

static double Percentage(uint64_t numerator, uint64_t denominator) {
  if (denominator == 0) {
    return 0.0;
  }
  return 100.0 * static_cast<double>(numerator) / static_cast<double>(denominator);
}

static void AddProgramStats(ProgramCacheStats* target, const ProgramCacheStats& source) {
  target->requests += source.requests;
  target->cacheHits += source.cacheHits;
  target->cacheMisses += source.cacheMisses;
  target->precompiledArtifactCreations += source.precompiledArtifactCreations;
  target->programBuilderCreations += source.programBuilderCreations;
  target->runtimePipelineCreationAttempts += source.runtimePipelineCreationAttempts;
  target->runtimePipelineCreationSuccesses += source.runtimePipelineCreationSuccesses;
  target->runtimePipelineCreationFailures += source.runtimePipelineCreationFailures;
}

static nlohmann::json ProgramStatsToJSON(const ProgramCacheStats& stats) {
  return {{"requests", stats.requests},
          {"cacheHits", stats.cacheHits},
          {"cacheMisses", stats.cacheMisses},
          {"precompiledArtifactCreations", stats.precompiledArtifactCreations},
          {"programBuilderCreations", stats.programBuilderCreations},
          {"runtimePipelineCreationAttempts", stats.runtimePipelineCreationAttempts},
          {"runtimePipelineCreationSuccesses", stats.runtimePipelineCreationSuccesses},
          {"runtimePipelineCreationFailures", stats.runtimePipelineCreationFailures}};
}

static nlohmann::json AOTStageCountsToJSON(const std::array<uint64_t, AOT_STAGE_COUNT>& counts) {
  nlohmann::json result = nlohmann::json::object();
  for (size_t i = 0; i < AOT_STAGE_COUNT; ++i) {
    result[PrecompiledAOTStageName(static_cast<PrecompiledAOTStage>(i))] = counts[i];
  }
  return result;
}

static nlohmann::json AOTStageCountsToJSON(const std::array<uint32_t, AOT_STAGE_COUNT>& counts) {
  nlohmann::json result = nlohmann::json::object();
  for (size_t i = 0; i < AOT_STAGE_COUNT; ++i) {
    result[PrecompiledAOTStageName(static_cast<PrecompiledAOTStage>(i))] = counts[i];
  }
  return result;
}

template <typename Count>
static bool AOTStagesAreMonotonic(const std::array<Count, AOT_STAGE_COUNT>& counts) {
  for (size_t i = 1; i < counts.size(); ++i) {
    if (counts[i] > counts[i - 1]) {
      return false;
    }
  }
  return true;
}

struct ConsistencyResult {
  bool consistent = false;
  nlohmann::json checks = {};
};

template <typename Count>
static ConsistencyResult BuildConsistencyChecks(
    const ProgramCacheStats& programStats, uint64_t artifactHits,
    const std::array<Count, AOT_STAGE_COUNT>& aotStageCounts,
    const std::array<Count, FALLBACK_REASON_COUNT>& fallbackCounts, uint64_t hitRecordCount,
    uint64_t fallbackRecordCount) {
  uint64_t fallbackEventCount = 0;
  for (auto count : fallbackCounts) {
    fallbackEventCount += count;
  }
  const auto attempts =
      static_cast<uint64_t>(aotStageCounts[static_cast<size_t>(PrecompiledAOTStage::Attempt)]);
  const auto artifactsFound = static_cast<uint64_t>(
      aotStageCounts[static_cast<size_t>(PrecompiledAOTStage::ArtifactsFound)]);
  const auto pipelinesCreated = static_cast<uint64_t>(
      aotStageCounts[static_cast<size_t>(PrecompiledAOTStage::PipelineCreated)]);
  const bool stagesMonotonic = AOTStagesAreMonotonic(aotStageCounts);
  const bool attemptsMatch = attempts == programStats.cacheMisses;
  const bool artifactHitsMatch = artifactHits == artifactsFound;
  const bool pipelineCreationsMatch = pipelinesCreated == programStats.precompiledArtifactCreations;
  const bool hitRecordsMatch = hitRecordCount == pipelinesCreated;
  const bool fallbackEventsMatch = fallbackEventCount == programStats.programBuilderCreations;
  const bool fallbackRecordsMatch = fallbackRecordCount == fallbackEventCount;

  ConsistencyResult result;
  // Program cache lookups and diagnostic AOT/fallback records are different event streams: one
  // logical ProgramInfo request can probe decomposed and plain keys, and Builder creations can occur
  // without a diagnostic fallback record. Keep those comparisons as legacy diagnostics, but do not
  // let them mark the epoch inconsistent. The stage/artifact/pipeline/hit-record chain is the
  // reliable same-epoch invariant.
  result.consistent =
      stagesMonotonic && artifactHitsMatch && pipelineCreationsMatch && hitRecordsMatch;
  result.checks = {
      {"consistent", result.consistent},
      {"aotStagesMonotonic", stagesMonotonic},
      {"aotAttemptsMatchProgramCacheMissesLegacy", attemptsMatch},
      {"artifactHitsMatchArtifactsFound", artifactHitsMatch},
      {"pipelineCreationsMatchPrecompiledPrograms", pipelineCreationsMatch},
      {"hitRecordsMatchPipelineCreations", hitRecordsMatch},
      {"fallbackEventsMatchProgramBuilderCreationsLegacy", fallbackEventsMatch},
      {"fallbackRecordsMatchFallbackEvents", fallbackRecordsMatch},
      {"deltas",
       {{"aotAttemptsMinusProgramCacheMisses",
         static_cast<int64_t>(attempts) - static_cast<int64_t>(programStats.cacheMisses)},
        {"artifactHitsMinusArtifactsFound",
         static_cast<int64_t>(artifactHits) - static_cast<int64_t>(artifactsFound)},
        {"pipelineCreationsMinusPrecompiledPrograms",
         static_cast<int64_t>(pipelinesCreated) -
             static_cast<int64_t>(programStats.precompiledArtifactCreations)},
        {"hitRecordsMinusPipelineCreations",
         static_cast<int64_t>(hitRecordCount) - static_cast<int64_t>(pipelinesCreated)},
        {"fallbackEventsMinusProgramBuilderCreations",
         static_cast<int64_t>(fallbackEventCount) -
             static_cast<int64_t>(programStats.programBuilderCreations)},
        {"fallbackRecordsMinusFallbackEvents",
         static_cast<int64_t>(fallbackRecordCount) - static_cast<int64_t>(fallbackEventCount)}}}};
  return result;
}

static nlohmann::json FallbackCountsToJSON(
    const std::array<uint64_t, FALLBACK_REASON_COUNT>& counts) {
  nlohmann::json result = nlohmann::json::object();
  for (size_t i = 0; i < FALLBACK_REASON_COUNT; ++i) {
    auto reason = static_cast<PrecompiledFallbackReason>(i);
    result[PrecompiledFallbackReasonName(reason)] = counts[i];
  }
  return result;
}

static nlohmann::json FallbackCountsToJSON(
    const std::array<uint32_t, FALLBACK_REASON_COUNT>& counts) {
  nlohmann::json result = nlohmann::json::object();
  for (size_t i = 0; i < FALLBACK_REASON_COUNT; ++i) {
    auto reason = static_cast<PrecompiledFallbackReason>(i);
    result[PrecompiledFallbackReasonName(reason)] = counts[i];
  }
  return result;
}

static std::string HitPermutationKey(const PrecompiledHitRecord& record) {
  std::stringstream stream;
  stream << record.shaderName << "\n"
         << record.vertPermutationIndex << "\n"
         << record.fragPermutationIndex;
  return stream.str();
}

static std::string HitEffectKey(const PrecompiledHitRecord& record) {
  std::stringstream stream;
  stream << record.shaderName << "\n" << record.effectSignature;
  return stream.str();
}

static std::string HitStructureKey(const PrecompiledHitRecord& record) {
  std::stringstream stream;
  stream << HitPermutationKey(record) << "\n" << record.pipelineSignature;
  return stream.str();
}

static std::string FallbackEffectKey(const PrecompiledFallbackRecord& record) {
  std::stringstream stream;
  stream << record.shaderName << "\n" << record.effectSignature;
  return stream.str();
}

static std::string FallbackEffectReasonKey(const PrecompiledFallbackRecord& record) {
  std::stringstream stream;
  stream << static_cast<uint32_t>(record.reason) << "\n" << FallbackEffectKey(record);
  return stream.str();
}

static std::string FallbackKey(const PrecompiledFallbackRecord& record) {
  std::stringstream stream;
  stream << static_cast<uint32_t>(record.reason) << "\n"
         << record.shaderName << "\n"
         << record.vertPermutationIndex << "\n"
         << record.fragPermutationIndex << "\n"
         << record.pipelineSignature;
  return stream.str();
}

static bool MoreFrequentShader(const AggregatedShader& left, const AggregatedShader& right) {
  if (left.count != right.count) {
    return left.count > right.count;
  }
  return left.shaderName < right.shaderName;
}

static bool MoreFrequentHit(const AggregatedHit& left, const AggregatedHit& right) {
  if (left.count != right.count) {
    return left.count > right.count;
  }
  return HitStructureKey(left.record) < HitStructureKey(right.record);
}

static bool MoreFrequentFallback(const AggregatedFallback& left, const AggregatedFallback& right) {
  if (left.count != right.count) {
    return left.count > right.count;
  }
  return left.record.pipelineSignature < right.record.pipelineSignature;
}

static nlohmann::json HitRecordToJSON(const PrecompiledHitRecord& record) {
  return {{"programKey", record.programKey},
          {"route", record.route},
          {"effect", record.effectSignature},
          {"pipeline", record.pipelineSignature},
          {"shader", record.shaderName},
          {"vertPermutationIndex", record.vertPermutationIndex},
          {"fragPermutationIndex", record.fragPermutationIndex}};
}

static bool ContainsRootCause(const std::string& effect, const std::string& token) {
  return effect.find(token) != std::string::npos;
}

static nlohmann::json BuildRootCauseProgramKeyAttribution(
    const std::vector<ShaderAOTTestResult>& results) {
  struct Row {
    uint64_t fallbacks = 0;
    uint64_t jitRecords = 0;
    std::set<std::string> tests;
    std::set<std::string> jitTests;
    std::set<std::string> effects;
    std::set<std::string> pipelines;
  };
  std::map<std::string, std::map<std::string, Row>> byCategory;
  for (const auto& result : results) {
    std::map<std::string, uint64_t> jitByKey;
    for (const auto& jit : result.jitProgramRecords) {
      jitByKey[jit.programKey + "\n" + std::to_string(jit.route)]++;
    }
    for (const auto& fallback : result.fallbackRecords) {
      if (fallback.reason != PrecompiledFallbackReason::NoMatchingRule) {
        continue;
      }
      std::string category;
      if (ContainsRootCause(fallback.effectSignature, "Gradient")) {
        category = "Gradient";
      } else if (ContainsRootCause(fallback.effectSignature, "ConstColorProcessor")) {
        category = "ConstColor";
      } else {
        continue;
      }
      auto key = fallback.programKey + "\n" + std::to_string(fallback.route);
      auto& row = byCategory[category][key];
      row.fallbacks++;
      row.tests.insert(result.testName);
      row.effects.insert(fallback.effectSignature);
      row.pipelines.insert(fallback.pipelineSignature);
      auto jit = jitByKey.find(key);
      if (jit != jitByKey.end()) {
        row.jitRecords += jit->second;
        row.jitTests.insert(result.testName);
      }
    }
  }
  nlohmann::json output = nlohmann::json::object();
  for (const auto& category : byCategory) {
    uint64_t fallbacks = 0;
    uint64_t jitRecords = 0;
    uint64_t matchedKeys = 0;
    nlohmann::json rows = nlohmann::json::array();
    for (const auto& item : category.second) {
      const auto& row = item.second;
      fallbacks += row.fallbacks;
      jitRecords += row.jitRecords;
      if (row.jitRecords > 0) matchedKeys++;
      auto keyEnd = item.first.find('\n');
      auto key = item.first.substr(0, keyEnd);
      auto route = std::stoul(item.first.substr(keyEnd + 1));
      rows.push_back({{"programKey", key},
                      {"route", route},
                      {"fallbackOccurrences", row.fallbacks},
                      {"jitCreationEvents", row.jitRecords},
                      {"jitKeyMatched", row.jitRecords > 0},
                      {"tests", row.tests},
                      {"jitTests", row.jitTests},
                      {"effects", row.effects},
                      {"pipelines", row.pipelines}});
    }
    output[category.first] = {{"fallbackOccurrences", fallbacks},
                              {"uniqueFallbackKeys", category.second.size()},
                              {"matchedJITKeys", matchedKeys},
                              {"jitCreationEvents", jitRecords},
                              {"attributionConfidence", "key-match-same-test"},
                              {"rows", std::move(rows)}};
  }
  return output;
}

static const char* AOTDecomposeOutcomeName(AOTDecomposeOutcome outcome) {
  switch (outcome) {
    case AOTDecomposeOutcome::Trivial:
      return "Trivial";
    case AOTDecomposeOutcome::FusablePointwise:
      return "FusablePointwise";
    case AOTDecomposeOutcome::BlockedByLowering:
      return "BlockedByLowering";
    case AOTDecomposeOutcome::BlockedByValidation:
      return "BlockedByValidation";
    case AOTDecomposeOutcome::UnsupportedShape:
      return "UnsupportedShape";
  }
  return "Unknown";
}

static nlohmann::json AxisAnalysisToJSON(const AOTAxisAnalysis& axis) {
  nlohmann::json json = {{"outcome", AOTDecomposeOutcomeName(axis.outcome)},
                         {"processorCount", axis.processorCount},
                         {"textureLeafCount", axis.textureLeafCount}};
  if (!axis.blockingProcessor.empty()) {
    json["blockingProcessor"] = axis.blockingProcessor;
  }
  return json;
}

static nlohmann::json FallbackRecordToJSON(const PrecompiledFallbackRecord& record) {
  nlohmann::json result = {{"programKey", record.programKey},
                           {"route", record.route},
                           {"reason", PrecompiledFallbackReasonName(record.reason)},
                           {"effect", record.effectSignature},
                           {"pipeline", record.pipelineSignature},
                           {"shader", record.shaderName}};
  auto invalidIndex = std::numeric_limits<uint32_t>::max();
  result["vertPermutationIndex"] = record.vertPermutationIndex == invalidIndex
                                       ? nlohmann::json(nullptr)
                                       : nlohmann::json(record.vertPermutationIndex);
  result["fragPermutationIndex"] = record.fragPermutationIndex == invalidIndex
                                       ? nlohmann::json(nullptr)
                                       : nlohmann::json(record.fragPermutationIndex);
  return result;
}

// Aggregates the per-draw decomposition feasibility analysis (attached to NoMatchingRule fallback
// records during diagnostic runs) into a machine-readable audit: per-axis outcome histograms, the
// blocking-fragment-processor histogram ranked by how many misses each unimplemented lowering
// blocks, and per-mechanism recoverable totals. Weighted by each unique structure's occurrence
// count so the numbers reflect real draw frequency, not just distinct shapes.
static nlohmann::json BuildDecompositionAudit(const std::vector<AggregatedFallback>& fallbacks,
                                              uint64_t* outFusableNow, uint64_t* outNeedsLowering) {
  std::map<std::string, uint64_t> colorOutcomes;
  std::map<std::string, uint64_t> coverageOutcomes;
  std::map<std::string, uint64_t> foldRouteOutcomes;
  std::map<std::string, uint64_t> blockingByFP;
  uint64_t analyzed = 0;
  uint64_t fusableNow = 0;
  uint64_t needsLowering = 0;
  uint64_t samplerBudgetExceeded = 0;
  for (const auto& fallback : fallbacks) {
    const auto& record = fallback.record;
    if (record.reason != PrecompiledFallbackReason::NoMatchingRule) {
      continue;
    }
    auto count = fallback.count;
    analyzed += count;
    foldRouteOutcomes[AOTFoldRouteOutcomeName(record.foldRouteOutcome)] += count;
    const auto& color = record.decomposeAnalysis.color;
    const auto& coverage = record.decomposeAnalysis.coverage;
    colorOutcomes[AOTDecomposeOutcomeName(color.outcome)] += count;
    coverageOutcomes[AOTDecomposeOutcomeName(coverage.outcome)] += count;
    if (color.outcome == AOTDecomposeOutcome::BlockedByLowering &&
        !color.blockingProcessor.empty()) {
      blockingByFP[color.blockingProcessor] += count;
    }
    if (coverage.outcome == AOTDecomposeOutcome::BlockedByLowering &&
        !coverage.blockingProcessor.empty()) {
      blockingByFP[coverage.blockingProcessor] += count;
    }
    bool colorFusable = color.outcome == AOTDecomposeOutcome::FusablePointwise;
    bool coverageServable = coverage.outcome == AOTDecomposeOutcome::Trivial ||
                            coverage.outcome == AOTDecomposeOutcome::FusablePointwise;
    if (colorFusable && coverageServable) {
      if (color.textureLeafCount + coverage.textureLeafCount > MaxFusedAOTSamplers) {
        samplerBudgetExceeded += count;
      } else {
        fusableNow += count;
      }
    } else if (color.outcome == AOTDecomposeOutcome::BlockedByLowering ||
               coverage.outcome == AOTDecomposeOutcome::BlockedByLowering) {
      needsLowering += count;
    }
  }
  nlohmann::json blockingJSON = nlohmann::json::array();
  for (const auto& item : blockingByFP) {
    blockingJSON.push_back({{"processor", item.first}, {"blockedMisses", item.second}});
  }
  std::sort(blockingJSON.begin(), blockingJSON.end(),
            [](const nlohmann::json& a, const nlohmann::json& b) {
              return a["blockedMisses"].get<uint64_t>() > b["blockedMisses"].get<uint64_t>();
            });
  if (outFusableNow != nullptr) {
    *outFusableNow = fusableNow;
  }
  if (outNeedsLowering != nullptr) {
    *outNeedsLowering = needsLowering;
  }
  return {{"analyzedMisses", analyzed},
          {"fusableNow", fusableNow},
          {"fusableButSamplerBudgetExceeded", samplerBudgetExceeded},
          {"needsNewLowering", needsLowering},
          {"colorOutcomes", colorOutcomes},
          {"coverageOutcomes", coverageOutcomes},
          {"foldRouteOutcomes", foldRouteOutcomes},
          {"blockingProcessorsRanked", std::move(blockingJSON)},
          {"fusedSamplerBudget", MaxFusedAOTSamplers}};
}

class ShaderAOTTestReporter : public testing::EmptyTestEventListener {
 public:
  void OnTestIterationStart(const testing::UnitTest&, int iteration) override {
    currentIteration = iteration;
    testResults.clear();
  }

  void OnTestStart(const testing::TestInfo&) override {
    currentBundleLoadedAtStart = false;
    currentProfileTag.clear();
    auto device = DevicePool::Make();
    if (device == nullptr) {
      return;
    }
    auto context = device->lockContext();
    if (context == nullptr) {
      return;
    }
    auto cache = context->precompiledShaderCache();
    if (!cache->isLoaded()) {
      auto bundle = EmbeddedShaderBundles::GetBundle(context->backend());
      if (bundle.first != nullptr && bundle.second > 0) {
        cache->loadBundle(bundle.first, bundle.second);
      }
    }
    currentBundleLoadedAtStart = cache->isLoaded();
    currentProfileTag = cache->profileTag();
    context->globalCache()->clearPrograms();
    context->globalCache()->resetProgramStats();
    cache->resetStats();
    cache->setDiagnosticRecordingEnabled(true);
    device->unlock();
  }

  void OnTestEnd(const testing::TestInfo& testInfo) override {
    ShaderAOTTestResult result;
    result.testName = std::string(testInfo.test_suite_name()) + "." + testInfo.name();
    result.passed = testInfo.result()->Passed();
    result.skipped = testInfo.result()->Skipped();
    auto device = DevicePool::Make();
    if (device != nullptr) {
      auto context = device->lockContext();
      if (context != nullptr) {
        result.contextAvailable = true;
        auto cache = context->precompiledShaderCache();
        result.bundleLoadedAtStart = currentBundleLoadedAtStart;
        result.bundleLoadedAtEnd = cache->isLoaded();
        result.profileTag = currentProfileTag;
        result.programStats = context->globalCache()->programStats();
        result.artifactHits = cache->hitCount();
        result.artifactMisses = cache->missCount();
        for (size_t i = 0; i < AOT_STAGE_COUNT; ++i) {
          result.aotStageCounts[i] = cache->aotStageCount(static_cast<PrecompiledAOTStage>(i));
        }
        for (size_t i = 0; i < FALLBACK_REASON_COUNT; ++i) {
          result.fallbackCounts[i] =
              cache->fallbackCount(static_cast<PrecompiledFallbackReason>(i));
        }
        result.hitRecords = cache->hitRecords();
        result.fallbackRecords = cache->fallbackRecords();
        result.jitProgramRecords = cache->jitProgramRecords();
        result.drawStats = cache->drawStats();
        result.offscreenFillStats = cache->offscreenFillStatsBySource();
        result.offscreenFillCorrelations = cache->offscreenFillCorrelations();
        cache->setDiagnosticRecordingEnabled(false);
        device->unlock();
      }
    }
    testResults.push_back(std::move(result));
  }

  void OnTestProgramEnd(const testing::UnitTest&) override {
    WriteReport();
  }

 private:
  int currentIteration = 0;
  bool currentBundleLoadedAtStart = false;
  std::string currentProfileTag;
  std::vector<ShaderAOTTestResult> testResults = {};

  void WriteReport() const {
    ShaderAOTSummary summary;
    uint64_t failedTestCount = 0;
    uint64_t skippedTestCount = 0;
    uint64_t testsWithColdPrograms = 0;
    uint64_t testsWithAOTHits = 0;
    uint64_t testsWithFallbacks = 0;
    uint64_t inconsistentTestCount = 0;
    uint64_t hitRecordCount = 0;
    uint64_t fallbackRecordCount = 0;
    std::map<std::string, AggregatedShader> shaderMap;
    std::map<std::string, AggregatedHit> permutationMap;
    std::map<std::string, AggregatedHit> hitEffectMap;
    std::map<std::string, AggregatedHit> hitStructureMap;
    std::set<std::string> fallbackEffectKeys;
    std::map<std::string, AggregatedFallback> fallbackEffectReasonMap;
    std::map<std::string, AggregatedFallback> fallbackMap;
    nlohmann::json testsJSON = nlohmann::json::array();
    for (const auto& testResult : testResults) {
      if (testResult.skipped) {
        skippedTestCount++;
      } else if (!testResult.passed) {
        failedTestCount++;
      }
      if (testResult.programStats.precompiledArtifactCreations +
              testResult.programStats.programBuilderCreations >
          0) {
        testsWithColdPrograms++;
      }
      if (!testResult.hitRecords.empty()) {
        testsWithAOTHits++;
      }
      if (!testResult.fallbackRecords.empty()) {
        testsWithFallbacks++;
      }
      AddProgramStats(&summary.programStats, testResult.programStats);
      AddDrawStats(&summary.drawStats, testResult.drawStats);
      for (size_t i = 0; i < OFFSCREEN_FILL_SOURCE_COUNT; ++i) {
        AddOffscreenFillStats(&summary.offscreenFillStats[i], testResult.offscreenFillStats[i]);
      }
      for (const auto& correlation : testResult.offscreenFillCorrelations) {
        AddOffscreenFillCorrelation(&summary.offscreenFillCorrelations, correlation);
      }
      summary.artifactHits += testResult.artifactHits;
      summary.artifactMisses += testResult.artifactMisses;
      for (size_t i = 0; i < AOT_STAGE_COUNT; ++i) {
        summary.aotStageCounts[i] += testResult.aotStageCounts[i];
      }
      for (size_t i = 0; i < FALLBACK_REASON_COUNT; ++i) {
        summary.fallbackCounts[i] += testResult.fallbackCounts[i];
      }

      nlohmann::json hitRecordsJSON = nlohmann::json::array();
      for (const auto& record : testResult.hitRecords) {
        hitRecordCount++;
        hitRecordsJSON.push_back(HitRecordToJSON(record));
        auto& shader = shaderMap[record.shaderName];
        shader.shaderName = record.shaderName;
        shader.count++;
        shader.tests.insert(testResult.testName);
        auto& permutation = permutationMap[HitPermutationKey(record)];
        permutation.record = record;
        permutation.count++;
        permutation.tests.insert(testResult.testName);
        auto& effect = hitEffectMap[HitEffectKey(record)];
        effect.record = record;
        effect.count++;
        effect.tests.insert(testResult.testName);
        auto& structure = hitStructureMap[HitStructureKey(record)];
        structure.record = record;
        structure.count++;
        structure.tests.insert(testResult.testName);
      }

      nlohmann::json fallbackRecordsJSON = nlohmann::json::array();
      for (const auto& record : testResult.fallbackRecords) {
        fallbackRecordCount++;
        fallbackRecordsJSON.push_back(FallbackRecordToJSON(record));
        fallbackEffectKeys.insert(FallbackEffectKey(record));
        auto& effect = fallbackEffectReasonMap[FallbackEffectReasonKey(record)];
        effect.record = record;
        effect.count++;
        effect.tests.insert(testResult.testName);
        auto& aggregate = fallbackMap[FallbackKey(record)];
        aggregate.record = record;
        aggregate.count++;
        aggregate.tests.insert(testResult.testName);
      }
      auto testConsistency =
          BuildConsistencyChecks(testResult.programStats, testResult.artifactHits,
                                 testResult.aotStageCounts, testResult.fallbackCounts,
                                 testResult.hitRecords.size(), testResult.fallbackRecords.size());
      if (!testConsistency.consistent) {
        inconsistentTestCount++;
      }
      testsJSON.push_back(
          {{"name", testResult.testName},
           {"passed", testResult.passed},
           {"skipped", testResult.skipped},
           {"contextAvailable", testResult.contextAvailable},
           {"bundleLoadedAtStart", testResult.bundleLoadedAtStart},
           {"bundleLoadedAtEnd", testResult.bundleLoadedAtEnd},
           {"profileTag", testResult.profileTag},
           {"program", ProgramStatsToJSON(testResult.programStats)},
           {"aotStages", AOTStageCountsToJSON(testResult.aotStageCounts)},
           {"drawMetrics", DrawStatsToJSON(testResult.drawStats)},
           {"artifactLookups",
            {{"hits", testResult.artifactHits}, {"misses", testResult.artifactMisses}}},
           {"consistencyChecks", std::move(testConsistency.checks)},
           {"hitRecords", std::move(hitRecordsJSON)},
           {"jitProgramRecords",
            [&]() {
              nlohmann::json records = nlohmann::json::array();
              for (const auto& record : testResult.jitProgramRecords) {
                records.push_back({{"programKey", record.programKey}, {"route", record.route}});
              }
              return records;
            }()},
           {"fallbackReasons", FallbackCountsToJSON(testResult.fallbackCounts)},
           {"fallbackRecords", std::move(fallbackRecordsJSON)}});
    }

    std::vector<AggregatedShader> sortedShaders;
    sortedShaders.reserve(shaderMap.size());
    for (const auto& item : shaderMap) {
      sortedShaders.push_back(item.second);
    }
    std::sort(sortedShaders.begin(), sortedShaders.end(), MoreFrequentShader);

    std::vector<AggregatedHit> sortedPermutations;
    sortedPermutations.reserve(permutationMap.size());
    for (const auto& item : permutationMap) {
      sortedPermutations.push_back(item.second);
    }
    std::sort(sortedPermutations.begin(), sortedPermutations.end(), MoreFrequentHit);

    std::vector<AggregatedHit> sortedHitEffects;
    sortedHitEffects.reserve(hitEffectMap.size());
    for (const auto& item : hitEffectMap) {
      sortedHitEffects.push_back(item.second);
    }
    std::sort(sortedHitEffects.begin(), sortedHitEffects.end(), MoreFrequentHit);

    std::vector<AggregatedHit> sortedHitStructures;
    sortedHitStructures.reserve(hitStructureMap.size());
    for (const auto& item : hitStructureMap) {
      sortedHitStructures.push_back(item.second);
    }
    std::sort(sortedHitStructures.begin(), sortedHitStructures.end(), MoreFrequentHit);

    std::vector<AggregatedFallback> sortedFallbackEffectsByReason;
    sortedFallbackEffectsByReason.reserve(fallbackEffectReasonMap.size());
    for (const auto& item : fallbackEffectReasonMap) {
      sortedFallbackEffectsByReason.push_back(item.second);
    }
    std::sort(sortedFallbackEffectsByReason.begin(), sortedFallbackEffectsByReason.end(),
              MoreFrequentFallback);

    std::vector<AggregatedFallback> sortedFallbacks;
    sortedFallbacks.reserve(fallbackMap.size());
    for (const auto& item : fallbackMap) {
      sortedFallbacks.push_back(item.second);
    }
    std::sort(sortedFallbacks.begin(), sortedFallbacks.end(), MoreFrequentFallback);

    nlohmann::json successfulShadersJSON = nlohmann::json::array();
    for (const auto& shader : sortedShaders) {
      successfulShadersJSON.push_back(
          {{"shader", shader.shaderName}, {"count", shader.count}, {"tests", shader.tests}});
    }
    nlohmann::json successfulPermutationsJSON = nlohmann::json::array();
    for (const auto& permutation : sortedPermutations) {
      successfulPermutationsJSON.push_back(
          {{"shader", permutation.record.shaderName},
           {"vertPermutationIndex", permutation.record.vertPermutationIndex},
           {"fragPermutationIndex", permutation.record.fragPermutationIndex},
           {"count", permutation.count},
           {"tests", permutation.tests}});
    }
    nlohmann::json successfulEffectsJSON = nlohmann::json::array();
    for (const auto& effect : sortedHitEffects) {
      successfulEffectsJSON.push_back({{"effect", effect.record.effectSignature},
                                       {"shader", effect.record.shaderName},
                                       {"count", effect.count},
                                       {"tests", effect.tests}});
    }
    nlohmann::json successfulStructuresJSON = nlohmann::json::array();
    for (const auto& structure : sortedHitStructures) {
      auto json = HitRecordToJSON(structure.record);
      json["count"] = structure.count;
      json["tests"] = structure.tests;
      successfulStructuresJSON.push_back(std::move(json));
    }
    nlohmann::json fallbackEffectsByReasonJSON = nlohmann::json::array();
    for (const auto& effect : sortedFallbackEffectsByReason) {
      fallbackEffectsByReasonJSON.push_back(
          {{"reason", PrecompiledFallbackReasonName(effect.record.reason)},
           {"effect", effect.record.effectSignature},
           {"shader", effect.record.shaderName},
           {"count", effect.count},
           {"tests", effect.tests}});
    }
    nlohmann::json fallbackStructuresJSON = nlohmann::json::array();
    for (const auto& fallback : sortedFallbacks) {
      auto json = FallbackRecordToJSON(fallback.record);
      json["count"] = fallback.count;
      json["tests"] = fallback.tests;
      if (fallback.record.reason == PrecompiledFallbackReason::NoMatchingRule) {
        json["decompose"] = {
            {"color", AxisAnalysisToJSON(fallback.record.decomposeAnalysis.color)},
            {"coverage", AxisAnalysisToJSON(fallback.record.decomposeAnalysis.coverage)}};
        json["foldRoute"] = AOTFoldRouteOutcomeName(fallback.record.foldRouteOutcome);
      }
      fallbackStructuresJSON.push_back(std::move(json));
    }

    const auto coldCreations = summary.programStats.precompiledArtifactCreations +
                               summary.programStats.programBuilderCreations;
    const auto artifactLookups = summary.artifactHits + summary.artifactMisses;
    auto globalConsistency =
        BuildConsistencyChecks(summary.programStats, summary.artifactHits, summary.aotStageCounts,
                               summary.fallbackCounts, hitRecordCount, fallbackRecordCount);

    uint64_t auditFusableNow = 0;
    uint64_t auditNeedsLowering = 0;
    auto decompositionAudit =
        BuildDecompositionAudit(sortedFallbacks, &auditFusableNow, &auditNeedsLowering);
    auto rootCauseProgramKeyAttribution = BuildRootCauseProgramKeyAttribution(testResults);

    nlohmann::json offscreenFillAudit = nlohmann::json::object();
    for (size_t i = 0; i < OFFSCREEN_FILL_SOURCE_COUNT; ++i) {
      auto source = static_cast<OffscreenFillSource>(i);
      offscreenFillAudit[OffscreenFillSourceName(source)] =
          OffscreenFillStatsToJSON(summary.offscreenFillStats[i]);
    }

    std::vector<OffscreenFillCorrelation> sortedOffscreenCorrelations = {};
    sortedOffscreenCorrelations.reserve(summary.offscreenFillCorrelations.size());
    for (const auto& [key, correlation] : summary.offscreenFillCorrelations) {
      sortedOffscreenCorrelations.push_back(correlation);
    }
    std::sort(sortedOffscreenCorrelations.begin(), sortedOffscreenCorrelations.end(),
              [](const auto& a, const auto& b) {
                return a.programBuilderCanExecute != b.programBuilderCanExecute
                           ? a.programBuilderCanExecute > b.programBuilderCanExecute
                           : a.calls > b.calls;
              });
    nlohmann::json offscreenFillCorrelations = nlohmann::json::array();
    for (const auto& correlation : sortedOffscreenCorrelations) {
      offscreenFillCorrelations.push_back(
          {{"source", OffscreenFillSourceName(correlation.source)},
           {"topLevelProcessor", correlation.topLevelProcessor},
           {"kernel", correlation.kernelSignature},
           {"calls", correlation.calls},
           {"canExecute", correlation.canExecute},
           {"precompiledPrograms", correlation.precompiledPrograms},
           {"programBuilderPrograms", correlation.programBuilderPrograms},
           {"precompiledCanExecute", correlation.precompiledCanExecute},
           {"programBuilderCanExecute", correlation.programBuilderCanExecute}});
    }

    nlohmann::json report = {
        {"backend", TGFX_BACKEND_NAME},
        {"iteration", currentIteration},
        {"metricDefinitions",
         {{"coldAOTHitRate",
           "precompiledArtifactCreations / (precompiledArtifactCreations + "
           "programBuilderCreations)"},
          {"artifactLookupHitRate", "artifactHits / (artifactHits + artifactMisses)"},
          {"successfulHit", "AOT render pipeline created successfully"},
          {"cacheHitsExcludedFromColdAOTRate", true}}},
        {"summary",
         {{"testCount", testResults.size()},
          {"failedTestCount", failedTestCount},
          {"skippedTestCount", skippedTestCount},
          {"allTestsConsistent", inconsistentTestCount == 0},
          {"inconsistentTestCount", inconsistentTestCount},
          {"testsWithColdPrograms", testsWithColdPrograms},
          {"testsWithAOTHits", testsWithAOTHits},
          {"testsWithFallbacks", testsWithFallbacks},
          {"program", ProgramStatsToJSON(summary.programStats)},
          {"aotStages", AOTStageCountsToJSON(summary.aotStageCounts)},
          {"drawMetrics", DrawStatsToJSON(summary.drawStats)},
          {"coldProgramCreations", coldCreations},
          {"coldAOTHitRate",
           Percentage(summary.programStats.precompiledArtifactCreations, coldCreations)},
          {"artifactLookups",
           {{"hits", summary.artifactHits},
            {"misses", summary.artifactMisses},
            {"hitRate", Percentage(summary.artifactHits, artifactLookups)}}},
          {"successfulHitCount", hitRecordCount},
          {"uniqueSuccessfulShaders", sortedShaders.size()},
          {"uniqueSuccessfulPermutations", sortedPermutations.size()},
          {"uniqueSuccessfulEffects", sortedHitEffects.size()},
          {"uniqueSuccessfulStructures", sortedHitStructures.size()},
          {"fallbackReasons", FallbackCountsToJSON(summary.fallbackCounts)},
          {"uniqueFallbackEffects", fallbackEffectKeys.size()},
          {"uniqueFallbackStructures", sortedFallbacks.size()}}},
        {"consistencyChecks", std::move(globalConsistency.checks)},
        {"successfulShaders", std::move(successfulShadersJSON)},
        {"successfulPermutations", std::move(successfulPermutationsJSON)},
        {"successfulEffects", std::move(successfulEffectsJSON)},
        {"successfulStructures", std::move(successfulStructuresJSON)},
        {"fallbackEffectsByReason", std::move(fallbackEffectsByReasonJSON)},
        {"fallbackStructures", std::move(fallbackStructuresJSON)},
        {"offscreenFillAudit", std::move(offscreenFillAudit)},
        {"offscreenFillCorrelations", std::move(offscreenFillCorrelations)},
        {"decompositionAudit", std::move(decompositionAudit)},
        {"rootCauseProgramKeyAttribution", std::move(rootCauseProgramKeyAttribution)},
        {"tests", std::move(testsJSON)}};

    auto outputPath = ProjectPath::Absolute(std::string("test/out/shader_aot_report.") +
                                            TGFX_BACKEND_NAME + ".json");
    std::filesystem::create_directories(std::filesystem::path(outputPath).parent_path());
    std::ofstream output(outputPath, std::ios::trunc);
    if (output.is_open()) {
      output << std::setw(2) << report << std::endl;
    }

    std::printf(
        "\n[Shader AOT][%s] cold=%llu precompiled=%llu fallback=%llu hitRate=%.2f%% "
        "cacheHits=%llu hitShaders=%zu hitEffects=%zu missEffects=%zu hitStructures=%zu "
        "missStructures=%zu\n",
        TGFX_BACKEND_NAME, static_cast<unsigned long long>(coldCreations),
        static_cast<unsigned long long>(summary.programStats.precompiledArtifactCreations),
        static_cast<unsigned long long>(summary.programStats.programBuilderCreations),
        Percentage(summary.programStats.precompiledArtifactCreations, coldCreations),
        static_cast<unsigned long long>(summary.programStats.cacheHits), sortedShaders.size(),
        sortedHitEffects.size(), fallbackEffectKeys.size(), sortedHitStructures.size(),
        sortedFallbacks.size());
    for (size_t i = 0; i < sortedShaders.size() && i < 20; ++i) {
      const auto& shader = sortedShaders[i];
      std::printf("  HIT %zu. count=%llu shader=%s\n", i + 1,
                  static_cast<unsigned long long>(shader.count), shader.shaderName.c_str());
    }
    for (size_t i = 0; i < sortedFallbacks.size() && i < 20; ++i) {
      const auto& fallback = sortedFallbacks[i];
      std::printf("  MISS %zu. count=%llu reason=%s shader=%s pipeline=%s\n", i + 1,
                  static_cast<unsigned long long>(fallback.count),
                  PrecompiledFallbackReasonName(fallback.record.reason),
                  fallback.record.shaderName.c_str(), fallback.record.pipelineSignature.c_str());
    }
    std::printf("  report=%s\n", outputPath.c_str());
    std::printf(
        "[Decompose Audit][%s] analyzed=%llu fusableNow=%llu needsNewLowering=%llu "
        "(see decompositionAudit.blockingProcessorsRanked)\n",
        TGFX_BACKEND_NAME, static_cast<unsigned long long>(auditFusableNow + auditNeedsLowering),
        static_cast<unsigned long long>(auditFusableNow),
        static_cast<unsigned long long>(auditNeedsLowering));
  }
};

void InstallShaderAOTTestReporter() {
  testing::UnitTest::GetInstance()->listeners().Append(new ShaderAOTTestReporter());
}

}  // namespace tgfx

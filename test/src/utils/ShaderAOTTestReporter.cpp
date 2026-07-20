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

struct ShaderAOTTestResult {
  std::string testName;
  bool passed = false;
  bool skipped = false;
  bool contextAvailable = false;
  bool bundleLoaded = false;
  std::string profileTag;
  ProgramCacheStats programStats = {};
  uint32_t artifactHits = 0;
  uint32_t artifactMisses = 0;
  std::array<uint32_t, FALLBACK_REASON_COUNT> fallbackCounts = {};
  std::vector<PrecompiledFallbackRecord> fallbackRecords = {};
};

struct ShaderAOTSummary {
  ProgramCacheStats programStats = {};
  uint64_t artifactHits = 0;
  uint64_t artifactMisses = 0;
  std::array<uint64_t, FALLBACK_REASON_COUNT> fallbackCounts = {};
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

static std::string FallbackKey(const PrecompiledFallbackRecord& record) {
  std::stringstream stream;
  stream << static_cast<uint32_t>(record.reason) << "\n"
         << record.shaderName << "\n"
         << record.vertPermutationIndex << "\n"
         << record.fragPermutationIndex << "\n"
         << record.pipelineSignature;
  return stream.str();
}

static bool MoreFrequentFallback(const AggregatedFallback& left, const AggregatedFallback& right) {
  if (left.count != right.count) {
    return left.count > right.count;
  }
  return left.record.pipelineSignature < right.record.pipelineSignature;
}

static nlohmann::json FallbackRecordToJSON(const PrecompiledFallbackRecord& record) {
  nlohmann::json result = {{"reason", PrecompiledFallbackReasonName(record.reason)},
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

class ShaderAOTTestReporter : public testing::EmptyTestEventListener {
 public:
  void OnTestIterationStart(const testing::UnitTest&, int iteration) override {
    currentIteration = iteration;
    testResults.clear();
  }

  void OnTestStart(const testing::TestInfo&) override {
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
        result.bundleLoaded = cache->isLoaded();
        result.profileTag = cache->profileTag();
        result.programStats = context->globalCache()->programStats();
        result.artifactHits = cache->hitCount();
        result.artifactMisses = cache->missCount();
        for (size_t i = 0; i < FALLBACK_REASON_COUNT; ++i) {
          result.fallbackCounts[i] =
              cache->fallbackCount(static_cast<PrecompiledFallbackReason>(i));
        }
        result.fallbackRecords = cache->fallbackRecords();
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
  std::vector<ShaderAOTTestResult> testResults = {};

  void WriteReport() const {
    ShaderAOTSummary summary;
    uint64_t failedTestCount = 0;
    uint64_t skippedTestCount = 0;
    uint64_t testsWithColdPrograms = 0;
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
      AddProgramStats(&summary.programStats, testResult.programStats);
      summary.artifactHits += testResult.artifactHits;
      summary.artifactMisses += testResult.artifactMisses;
      for (size_t i = 0; i < FALLBACK_REASON_COUNT; ++i) {
        summary.fallbackCounts[i] += testResult.fallbackCounts[i];
      }
      nlohmann::json recordsJSON = nlohmann::json::array();
      for (const auto& record : testResult.fallbackRecords) {
        recordsJSON.push_back(FallbackRecordToJSON(record));
        auto& aggregate = fallbackMap[FallbackKey(record)];
        aggregate.record = record;
        aggregate.count++;
        aggregate.tests.insert(testResult.testName);
      }
      testsJSON.push_back(
          {{"name", testResult.testName},
           {"passed", testResult.passed},
           {"skipped", testResult.skipped},
           {"contextAvailable", testResult.contextAvailable},
           {"bundleLoadedAtEnd", testResult.bundleLoaded},
           {"profileTag", testResult.profileTag},
           {"program", ProgramStatsToJSON(testResult.programStats)},
           {"artifactLookups",
            {{"hits", testResult.artifactHits}, {"misses", testResult.artifactMisses}}},
           {"fallbackReasons", FallbackCountsToJSON(testResult.fallbackCounts)},
           {"fallbackRecords", std::move(recordsJSON)}});
    }

    std::vector<AggregatedFallback> sortedFallbacks;
    sortedFallbacks.reserve(fallbackMap.size());
    for (const auto& item : fallbackMap) {
      sortedFallbacks.push_back(item.second);
    }
    std::sort(sortedFallbacks.begin(), sortedFallbacks.end(), MoreFrequentFallback);

    nlohmann::json fallbackStructuresJSON = nlohmann::json::array();
    for (const auto& fallback : sortedFallbacks) {
      auto json = FallbackRecordToJSON(fallback.record);
      json["count"] = fallback.count;
      json["tests"] = fallback.tests;
      fallbackStructuresJSON.push_back(std::move(json));
    }

    auto coldCreations = summary.programStats.precompiledArtifactCreations +
                         summary.programStats.programBuilderCreations;
    auto artifactLookups = summary.artifactHits + summary.artifactMisses;
    nlohmann::json report = {
        {"backend", TGFX_BACKEND_NAME},
        {"iteration", currentIteration},
        {"metricDefinitions",
         {{"coldAOTHitRate",
           "precompiledArtifactCreations / (precompiledArtifactCreations + "
           "programBuilderCreations)"},
          {"artifactLookupHitRate", "artifactHits / (artifactHits + artifactMisses)"},
          {"cacheHitsExcludedFromColdAOTRate", true}}},
        {"summary",
         {{"testCount", testResults.size()},
          {"failedTestCount", failedTestCount},
          {"skippedTestCount", skippedTestCount},
          {"testsWithColdPrograms", testsWithColdPrograms},
          {"program", ProgramStatsToJSON(summary.programStats)},
          {"coldProgramCreations", coldCreations},
          {"coldAOTHitRate",
           Percentage(summary.programStats.precompiledArtifactCreations, coldCreations)},
          {"artifactLookups",
           {{"hits", summary.artifactHits},
            {"misses", summary.artifactMisses},
            {"hitRate", Percentage(summary.artifactHits, artifactLookups)}}},
          {"fallbackReasons", FallbackCountsToJSON(summary.fallbackCounts)},
          {"uniqueFallbackStructures", sortedFallbacks.size()}}},
        {"fallbackStructures", std::move(fallbackStructuresJSON)},
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
        "cacheHits=%llu uniqueMisses=%zu\n",
        TGFX_BACKEND_NAME, static_cast<unsigned long long>(coldCreations),
        static_cast<unsigned long long>(summary.programStats.precompiledArtifactCreations),
        static_cast<unsigned long long>(summary.programStats.programBuilderCreations),
        Percentage(summary.programStats.precompiledArtifactCreations, coldCreations),
        static_cast<unsigned long long>(summary.programStats.cacheHits), sortedFallbacks.size());
    for (size_t i = 0; i < sortedFallbacks.size() && i < 20; ++i) {
      const auto& fallback = sortedFallbacks[i];
      std::printf("  %zu. count=%llu reason=%s shader=%s pipeline=%s\n", i + 1,
                  static_cast<unsigned long long>(fallback.count),
                  PrecompiledFallbackReasonName(fallback.record.reason),
                  fallback.record.shaderName.c_str(), fallback.record.pipelineSignature.c_str());
    }
    std::printf("  report=%s\n", outputPath.c_str());
  }
};

void InstallShaderAOTTestReporter() {
  testing::UnitTest::GetInstance()->listeners().Append(new ShaderAOTTestReporter());
}

}  // namespace tgfx

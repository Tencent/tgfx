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
//  Unless required by applicable law or agreed to in writing, software distributed under
//  the license is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
//  KIND, either express or implied. see the license for the specific language governing
//  permissions and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "ReportWriter.h"

#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <tuple>
#include <utility>
#include "StageReport.h"
#include "gpu/ShaderKeyHash.h"

namespace tgfx {

namespace {

struct ShaderKeyHashLess {
  bool operator()(const ShaderKeyHash& left, const ShaderKeyHash& right) const {
    if (left.hi != right.hi) {
      return left.hi < right.hi;
    }
    return left.lo < right.lo;
  }
};

struct StageContentStats {
  uint64_t uniqueCount = 0;
  uint64_t uniqueBytes = 0;
  std::map<ShaderKeyHash, std::vector<const std::vector<uint8_t>*>, ShaderKeyHashLess> buckets;

  void add(const std::vector<uint8_t>& blob) {
    auto hash = ComputeBlobHash(blob);
    auto& bucket = buckets[hash];
    for (const auto* existing : bucket) {
      if (*existing == blob) {
        return;
      }
    }
    bucket.push_back(&blob);
    uniqueCount++;
    uniqueBytes += blob.size();
  }
};

struct ProfileArtifactStats {
  std::set<std::pair<std::string, uint32_t>> logicalVertices;
  std::set<std::pair<std::string, uint32_t>> logicalFragments;
  uint64_t logicalVertexBytes = 0;
  uint64_t logicalFragmentBytes = 0;
  uint64_t errorCount = 0;
  StageContentStats vertexContents;
  StageContentStats fragmentContents;
};

std::map<std::string, ProfileArtifactStats> CollectArtifactStats(
    const std::vector<VariantData>& variants,
    const std::map<std::string, uint64_t>& profileErrorCounts) {
  std::map<std::string, ProfileArtifactStats> result;
  for (const auto& profile : profileErrorCounts) {
    result[profile.first].errorCount = profile.second;
  }
  for (const auto& variant : variants) {
    auto& stats = result[variant.profileTag];
    auto vertexKey = std::make_pair(variant.shaderName, variant.vertPermutationIndex);
    if (stats.logicalVertices.insert(vertexKey).second) {
      stats.logicalVertexBytes += variant.vertexBlob.size();
      stats.vertexContents.add(variant.vertexBlob);
    }
    auto fragmentKey = std::make_pair(variant.shaderName, variant.fragPermutationIndex);
    if (stats.logicalFragments.insert(fragmentKey).second) {
      stats.logicalFragmentBytes += variant.fragmentBlob.size();
      stats.fragmentContents.add(variant.fragmentBlob);
    }
  }
  return result;
}

}  // namespace

bool WriteReportJson(const BuildReport& report, const std::string& outDir) {
  std::string path = outDir + "/shader_build_report.json";
  std::ofstream file(path);
  if (!file.is_open()) {
    std::cerr << "Failed to open output file: " << path << "\n";
    return false;
  }
  auto artifactStats = CollectArtifactStats(report.variants, report.diagnostics.errorCounts());
  // Per-shader logical stage bytes: each unique (shader, permutationIndex, profile) stage
  // counts once. The profile is part of the key because the same permutation compiles to a
  // different blob per backend (SPIR-V vs metallib vs WGSL): without it, the first backend
  // processed would define the reported figure and the number would drift with backend order.
  std::map<std::string, std::pair<uint64_t, uint64_t>> shaderStageBytes;
  std::set<std::tuple<std::string, uint32_t, std::string>> seenVerts;
  std::set<std::tuple<std::string, uint32_t, std::string>> seenFrags;
  for (const auto& variant : report.variants) {
    if (seenVerts.insert({variant.shaderName, variant.vertPermutationIndex, variant.profileTag})
            .second) {
      shaderStageBytes[variant.shaderName].first += variant.vertexBlob.size();
    }
    if (seenFrags.insert({variant.shaderName, variant.fragPermutationIndex, variant.profileTag})
            .second) {
      shaderStageBytes[variant.shaderName].second += variant.fragmentBlob.size();
    }
  }
  file << "{\n  \"shaders\": [\n";
  for (size_t i = 0; i < report.shaders.size(); i++) {
    const auto& shader = report.shaders[i];
    auto bytesIt = shaderStageBytes.find(shader.name);
    uint64_t vertexBytes = bytesIt == shaderStageBytes.end() ? 0 : bytesIt->second.first;
    uint64_t fragmentBytes = bytesIt == shaderStageBytes.end() ? 0 : bytesIt->second.second;
    file << "    {\n";
    file << "      \"name\": \"" << shader.name << "\",\n";
    file << "      \"rawCount\": " << shader.rawCount << ",\n";
    file << "      \"compiledCount\": " << shader.compiledCount << ",\n";
    file << "      \"vertexBytes\": " << vertexBytes << ",\n";
    file << "      \"fragmentBytes\": " << fragmentBytes << ",\n";
    file << "      \"errorCount\": " << shader.errorCount << "\n";
    file << "    }";
    if (i + 1 < report.shaders.size()) {
      file << ",";
    }
    file << "\n";
  }
  file << "  ],\n  \"artifactProfiles\": [\n";
  size_t profileIndex = 0;
  for (const auto& profile : artifactStats) {
    const auto& stats = profile.second;
    file << "    {\n";
    file << "      \"profile\": \"" << profile.first << "\",\n";
    file << "      \"logicalVertexCount\": " << stats.logicalVertices.size() << ",\n";
    file << "      \"uniqueVertexCount\": " << stats.vertexContents.uniqueCount << ",\n";
    file << "      \"logicalVertexBytes\": " << stats.logicalVertexBytes << ",\n";
    file << "      \"uniqueVertexBytes\": " << stats.vertexContents.uniqueBytes << ",\n";
    file << "      \"logicalFragmentCount\": " << stats.logicalFragments.size() << ",\n";
    file << "      \"uniqueFragmentCount\": " << stats.fragmentContents.uniqueCount << ",\n";
    file << "      \"logicalFragmentBytes\": " << stats.logicalFragmentBytes << ",\n";
    file << "      \"uniqueFragmentBytes\": " << stats.fragmentContents.uniqueBytes << ",\n";
    file << "      \"errorCount\": " << stats.errorCount << "\n";
    file << "    }";
    profileIndex++;
    if (profileIndex < artifactStats.size()) {
      file << ",";
    }
    file << "\n";
  }
  file << "  ]\n}\n";
  file.close();
  std::cout << "Report written to: " << path << "\n";
  return true;
}

}  // namespace tgfx

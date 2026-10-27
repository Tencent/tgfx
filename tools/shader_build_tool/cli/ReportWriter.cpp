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
#include "output/StageReport.h"
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

const char* SeverityPrefix(Severity severity) {
  switch (severity) {
    case Severity::Error:
      return "E";
    case Severity::Warning:
      return "W";
    case Severity::Unverifiable:
      return "U";
  }
  return "E";
}

const char* SeverityName(Severity severity) {
  switch (severity) {
    case Severity::Error:
      return "error";
    case Severity::Warning:
      return "warning";
    case Severity::Unverifiable:
      return "unverifiable";
  }
  return "error";
}

const char* LayerName(Layer layer) {
  switch (layer) {
    case Layer::Sources:
      return "sources";
    case Layer::Plan:
      return "plan";
    case Layer::Compile:
      return "compile";
    case Layer::Emit:
      return "emit";
    case Layer::Verify:
      return "verify";
    case Layer::Output:
      return "output";
    case Layer::Cli:
      return "cli";
  }
  return "cli";
}

// Escapes a string for the JSON report: quotes, backslashes and control characters. Diagnostics
// carry compiler stderr, which contains newlines and quotes.
std::string JsonEscape(const std::string& text) {
  std::string escaped;
  escaped.reserve(text.size());
  for (char ch : text) {
    switch (ch) {
      case '"':
        escaped += "\\\"";
        break;
      case '\\':
        escaped += "\\\\";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(ch) < 0x20) {
          escaped += "\\u00";
          const char* hex = "0123456789abcdef";
          escaped += hex[(static_cast<unsigned char>(ch) >> 4) & 0xf];
          escaped += hex[static_cast<unsigned char>(ch) & 0xf];
        } else {
          escaped += ch;
        }
        break;
    }
  }
  return escaped;
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
  file << "  ],\n  \"diagnostics\": [\n";
  // Every diagnostic the build recorded, in report order: the machine-readable counterpart of
  // the human lines cli prints. A failing build writes this array (the report is a debugging
  // input, not an artifact), while the bundle and manifest are not written at all.
  for (size_t i = 0; i < report.diagnostics.records().size(); i++) {
    const auto& record = report.diagnostics.records()[i];
    file << "    {\n";
    file << "      \"severity\": \"" << SeverityName(record.severity) << "\",\n";
    file << "      \"layer\": \"" << LayerName(record.layer) << "\",\n";
    file << "      \"code\": \"" << JsonEscape(record.code != nullptr ? record.code : "") << "\",\n";
    file << "      \"shader\": \"" << JsonEscape(record.shader) << "\",\n";
    file << "      \"profile\": \"" << JsonEscape(record.profile) << "\",\n";
    file << "      \"vert\": " << record.vertPermutationIndex << ",\n";
    file << "      \"frag\": " << record.fragPermutationIndex << ",\n";
    file << "      \"message\": \"" << JsonEscape(record.message) << "\"\n";
    file << "    }";
    if (i + 1 < report.diagnostics.records().size()) {
      file << ",";
    }
    file << "\n";
  }
  file << "  ]\n}\n";
  file.close();
  std::cout << "Report written to: " << path << "\n";
  return true;
}

void PrintDiagnostics(const Diagnostics& diagnostics) {
  for (const auto& record : diagnostics.records()) {
    if (record.message.empty()) {
      continue;
    }
    std::cerr << "[" << SeverityPrefix(record.severity) << "][" << LayerName(record.layer) << "] "
              << (record.code != nullptr ? record.code : "");
    if (!record.shader.empty()) {
      std::cerr << "  shader=" << record.shader;
    }
    if (!record.profile.empty()) {
      std::cerr << " profile=" << record.profile;
    }
    if (record.vertPermutationIndex >= 0) {
      std::cerr << " vert=" << record.vertPermutationIndex;
    }
    if (record.fragPermutationIndex >= 0) {
      std::cerr << " frag=" << record.fragPermutationIndex;
    }
    std::cerr << "\n  " << record.message << "\n";
  }
}

}  // namespace tgfx

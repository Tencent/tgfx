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
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. See the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "StageReport.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include "gpu/shaders/PrecompiledShader.h"

namespace tgfx {
namespace {

struct StageRecord {
  std::string profileTag;
  std::string shaderName;
  bool isVertex = false;
  uint32_t permutationIndex = 0;
  std::vector<uint8_t> codeBlob;
  std::vector<uint8_t> reflectionBlob;
  std::string identity;
  std::string duplicateOf;
};

std::string StageSuffix(bool isVertex) {
  return isVertex ? "_Vert" : "_Frag";
}

std::string FormatHashHex(const ShaderKeyHash& hash) {
  char buffer[33];
  std::snprintf(buffer, sizeof(buffer), "%016llx%016llx", static_cast<unsigned long long>(hash.hi),
                static_cast<unsigned long long>(hash.lo));
  return buffer;
}

// Duplicate detection groups records by code hash within one profile; bucket members are then
// compared byte for byte (code and serialized reflection), so a hash collision can never merge
// two distinct stages silently.
struct CodeBucketLess {
  bool operator()(const ShaderKeyHash& left, const ShaderKeyHash& right) const {
    if (left.hi != right.hi) {
      return left.hi < right.hi;
    }
    return left.lo < right.lo;
  }
};

bool SameStageContent(const StageRecord& left, const StageRecord& right) {
  return left.codeBlob == right.codeBlob && left.reflectionBlob == right.reflectionBlob;
}

struct RecordOrder {
  bool operator()(const StageRecord& left, const StageRecord& right) const {
    // Profile first so per-profile summaries read as contiguous blocks, then family, then
    // fragment before vertex to mirror the pool order a reader sees in the bundle.
    return std::tie(left.profileTag, left.shaderName, left.isVertex, left.permutationIndex) <
           std::tie(right.profileTag, right.shaderName, right.isVertex, right.permutationIndex);
  }
};

}  // namespace

bool WriteStageReport(const std::string& path, const std::vector<VariantData>& variants) {
  // One record per logical stage (family, stage, permutation index, profile); variants that
  // share a stage keep the first occurrence, which is also how the bundle writer dedupes.
  std::vector<StageRecord> records;
  std::set<std::tuple<std::string, std::string, bool, uint32_t>> seen;
  for (const auto& variant : variants) {
    auto vertexKey =
        std::make_tuple(variant.profileTag, variant.shaderName, true, variant.vertPermutationIndex);
    if (seen.insert(vertexKey).second) {
      StageRecord record;
      record.profileTag = variant.profileTag;
      record.shaderName = variant.shaderName;
      record.isVertex = true;
      record.permutationIndex = variant.vertPermutationIndex;
      record.codeBlob = variant.vertexBlob;
      record.reflectionBlob = SerializeStageReflection(variant.vertexReflection);
      records.push_back(std::move(record));
    }
    auto fragmentKey = std::make_tuple(variant.profileTag, variant.shaderName, false,
                                       variant.fragPermutationIndex);
    if (seen.insert(fragmentKey).second) {
      StageRecord record;
      record.profileTag = variant.profileTag;
      record.shaderName = variant.shaderName;
      record.isVertex = false;
      record.permutationIndex = variant.fragPermutationIndex;
      record.codeBlob = variant.fragmentBlob;
      record.reflectionBlob = SerializeStageReflection(variant.fragmentReflection);
      records.push_back(std::move(record));
    }
  }
  std::sort(records.begin(), records.end(), RecordOrder());

  // Cross-family duplicate attribution inside each profile. A record whose code AND reflection
  // both match an earlier record references that record's identity; the first occurrence stays
  // empty. Code-only matches surface through the summary counts instead.
  std::map<std::string, std::map<ShaderKeyHash, std::vector<const StageRecord*>, CodeBucketLess>>
      bucketsByProfile;
  std::map<std::string, std::set<std::vector<uint8_t>>> uniqueCodeByProfile;
  std::map<std::string, size_t> uniqueCodeReflectionByProfile;
  std::map<std::string, size_t> logicalStagesByProfile;
  for (auto& record : records) {
    record.identity = record.profileTag + "/" + record.shaderName + StageSuffix(record.isVertex) +
                      "[" + std::to_string(record.permutationIndex) + "]";
    auto& buckets = bucketsByProfile[record.profileTag];
    auto codeHash = ComputeBlobHash(record.codeBlob);
    auto& bucket = buckets[codeHash];
    const StageRecord* firstMatch = nullptr;
    for (const StageRecord* existing : bucket) {
      if (SameStageContent(*existing, record)) {
        firstMatch = existing;
        break;
      }
    }
    if (firstMatch != nullptr) {
      record.duplicateOf = firstMatch->identity;
    } else {
      bucket.push_back(&record);
      uniqueCodeReflectionByProfile[record.profileTag]++;
    }
    uniqueCodeByProfile[record.profileTag].insert(record.codeBlob);
    logicalStagesByProfile[record.profileTag]++;
  }

  std::ofstream file(path);
  if (!file.is_open()) {
    std::cerr << "Failed to open stage report output file: " << path << "\n";
    return false;
  }
  file << "{\n  \"stages\": [\n";
  size_t recordIndex = 0;
  for (const auto& record : records) {
    auto* info = ShaderRegistry::Find(record.shaderName);
    if (info == nullptr) {
      std::cerr << "Stage report: no registry entry for " << record.shaderName << "\n";
      return false;
    }
    const auto& domain = record.isVertex ? info->vertDomain : info->fragDomain;
    auto codeHash = ComputeBlobHash(record.codeBlob);
    auto stageKey = ComputeShaderKeyHash(record.shaderName + StageSuffix(record.isVertex),
                                         record.permutationIndex, record.profileTag);
    file << "    {\n";
    file << "      \"identity\": \"" << record.identity << "\",\n";
    file << "      \"shader\": \"" << record.shaderName << "\",\n";
    file << "      \"stage\": \"" << (record.isVertex ? "vert" : "frag") << "\",\n";
    file << "      \"profile\": \"" << record.profileTag << "\",\n";
    file << "      \"permutationIndex\": " << record.permutationIndex << ",\n";
    file << "      \"stageKey\": \"" << FormatHashHex(stageKey) << "\",\n";
    file << "      \"dimensions\": [";
    const auto& defines = domain.defineListFor(record.permutationIndex);
    for (size_t i = 0; i < defines.size(); i++) {
      file << (i == 0 ? "" : ", ") << "\"" << defines[i] << "\"";
    }
    file << "],\n";
    file << "      \"codeBytes\": " << record.codeBlob.size() << ",\n";
    file << "      \"codeHash\": \"" << FormatHashHex(codeHash) << "\",\n";
    file << "      \"reflectionHash\": \"" << FormatHashHex(ComputeBlobHash(record.reflectionBlob))
         << "\",\n";
    if (record.duplicateOf.empty()) {
      file << "      \"duplicateOf\": null\n";
    } else {
      file << "      \"duplicateOf\": \"" << record.duplicateOf << "\"\n";
    }
    file << "    }";
    if (++recordIndex < records.size()) {
      file << ",";
    }
    file << "\n";
  }
  file << "  ],\n  \"profiles\": [\n";
  size_t profileIndex = 0;
  for (const auto& entry : logicalStagesByProfile) {
    const auto& profile = entry.first;
    file << "    {\n";
    file << "      \"profile\": \"" << profile << "\",\n";
    file << "      \"logicalStages\": " << entry.second << ",\n";
    file << "      \"uniqueCode\": " << uniqueCodeByProfile[profile].size() << ",\n";
    file << "      \"uniqueCodeReflection\": " << uniqueCodeReflectionByProfile[profile] << "\n";
    file << "    }";
    if (++profileIndex < logicalStagesByProfile.size()) {
      file << ",";
    }
    file << "\n";
  }
  file << "  ]\n}\n";
  file.close();
  if (!file.good()) {
    std::cerr << "Stage report write failed (disk full or I/O failure): " << path << "\n";
    return false;
  }
  std::cout << "Stage report written to: " << path << "\n";
  return true;
}

}  // namespace tgfx

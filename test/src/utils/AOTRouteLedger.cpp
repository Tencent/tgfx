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
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions and
//  limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "utils/AOTRouteLedger.h"
#include <fstream>
#include <sstream>

namespace tgfx {
namespace {

// The ledger's fields are plain identifiers, so a minimal escaper suffices; it keeps the writer
// dependency-free instead of pulling a JSON library into the test target.
std::string JsonString(const std::string& text) {
  std::string out = "\"";
  for (char c : text) {
    switch (c) {
      case '\\':
        out += "\\\\";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\n':
        out += "\\n";
        break;
      default:
        out += c;
        break;
    }
  }
  out += "\"";
  return out;
}

}  // namespace

bool SaveRouteLedgerJSON(const std::string& path,
                         const std::vector<RouteLedgerSceneStats>& entries) {
  std::ostringstream stream = {};
  stream << "{\n  \"scenes\": [\n";
  for (size_t i = 0; i < entries.size(); ++i) {
    const auto& entry = entries[i];
    stream << "    {\"scene\": " << JsonString(entry.scene) << ", \"shaders\": [";
    for (size_t j = 0; j < entry.shaderHits.size(); ++j) {
      if (j > 0) {
        stream << ", ";
      }
      stream << "{\"name\": " << JsonString(entry.shaderHits[j].first)
             << ", \"hits\": " << entry.shaderHits[j].second << "}";
    }
    stream << "], \"runtimeCompiles\": " << entry.runtimeCompiles
           << ", \"noMatchingRule\": " << entry.noMatchingRule << ", \"draws\": " << entry.draws
           << ", \"completeAOTDraws\": " << entry.completeAOTDraws
           << ", \"droppedDraws\": " << entry.droppedDraws
           << ", \"directChainDraws\": " << entry.directChainDraws
           << ", \"offscreenPlanDraws\": " << entry.offscreenPlanDraws
           << ", \"atomicFallbacks\": " << entry.atomicFallbacks
           << ", \"planExecutionFailures\": " << entry.planExecutionFailures
           << ", \"materializedEdges\": " << entry.materializedEdges << ", \"planPassHistogram\": ["
           << entry.planPassHistogram[0] << ", " << entry.planPassHistogram[1] << ", "
           << entry.planPassHistogram[2] << ", " << entry.planPassHistogram[3] << "]}";
    if (i + 1 < entries.size()) {
      stream << ",";
    }
    stream << "\n";
  }
  stream << "  ]\n}\n";
  std::ofstream file(path, std::ios::binary);
  if (!file) {
    return false;
  }
  file << stream.str();
  return file.good();
}

bool SaveClipPortProbeJSON(const std::string& path,
                           const std::vector<ClipPortProbeResult>& results) {
  std::ostringstream stream = {};
  stream << "{\n  \"cases\": [\n";
  for (size_t i = 0; i < results.size(); ++i) {
    const auto& result = results[i];
    stream << "    {\"geometry\": " << JsonString(result.geometry)
           << ", \"clip\": " << JsonString(result.clip)
           << ", \"blend\": " << JsonString(result.blend)
           << ", \"chainServed\": " << (result.chainServed ? "true" : "false")
           << ", \"droppedDraws\": " << result.droppedDraws
           << ", \"runtimeCompiles\": " << result.runtimeCompiles
           << ", \"maxChannelDiff\": " << result.maxChannelDiff
           << ", \"diffBytes\": " << result.diffBytes << ", \"totalBytes\": " << result.totalBytes
           << "}";
    if (i + 1 < results.size()) {
      stream << ",";
    }
    stream << "\n";
  }
  stream << "  ]\n}\n";
  std::ofstream file(path, std::ios::binary);
  if (!file) {
    return false;
  }
  file << stream.str();
  return file.good();
}

}  // namespace tgfx

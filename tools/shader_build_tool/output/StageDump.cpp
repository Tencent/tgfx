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
//  either express or implied. See the License for the specific language governing permissions and
//  limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "output/StageDump.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include "core/DiagnosticCodes.h"
#include "gpu/ShaderKeyHash.h"

namespace tgfx {

namespace {

void ReportWriteFailure(Diagnostics* diagnostics, const std::string& path) {
  Diagnostic diagnostic;
  diagnostic.severity = Severity::Error;
  diagnostic.layer = Layer::Output;
  diagnostic.code = DiagnosticCode::OutputWrite;
  diagnostic.message = "cannot write the stage dump file " + path;
  diagnostics->report(std::move(diagnostic));
}

std::string FormatHashHex(const ShaderKeyHash& hash) {
  std::stringstream stream;
  stream << std::hex << std::setfill('0') << std::setw(16) << hash.hi << std::setw(16) << hash.lo;
  return stream.str();
}

bool WriteTextFile(const std::filesystem::path& path, const std::string& text,
                   Diagnostics* diagnostics) {
  std::ofstream file(path, std::ios::binary);
  if (!file.is_open()) {
    ReportWriteFailure(diagnostics, path.string());
    return false;
  }
  file.write(text.data(), static_cast<std::streamsize>(text.size()));
  return true;
}

void WriteUniformEntries(std::ofstream& file, const std::vector<UniformEntry>& entries) {
  file << "[";
  for (size_t index = 0; index < entries.size(); ++index) {
    if (index > 0) {
      file << ",";
    }
    file << "\n    {\"name\": \"" << entries[index].name << "\", \"format\": "
         << static_cast<int>(entries[index].format) << ", \"arraySize\": " << entries[index].arraySize
         << "}";
  }
  if (!entries.empty()) {
    file << "\n  ";
  }
  file << "]";
}

void WriteVertexInputEntries(std::ofstream& file, const std::vector<VertexInputEntry>& entries) {
  file << "[";
  for (size_t index = 0; index < entries.size(); ++index) {
    if (index > 0) {
      file << ",";
    }
    file << "\n    {\"name\": \"" << entries[index].name << "\", \"format\": "
         << static_cast<int>(entries[index].format)
         << ", \"location\": " << static_cast<int>(entries[index].location) << "}";
  }
  if (!entries.empty()) {
    file << "\n  ";
  }
  file << "]";
}

void WriteStage(std::ofstream& file, const char* stageName, const std::vector<uint8_t>& blob,
                const StageReflectionData& reflection) {
  file << "  \"" << stageName << "\": {\n";
  file << "    \"codeHash\": \"" << FormatHashHex(ComputeBlobHash(blob)) << "\",\n";
  file << "    \"uniforms\": ";
  WriteUniformEntries(file, reflection.uniforms);
  file << ",\n    \"samplers\": ";
  WriteUniformEntries(file, reflection.samplers);
  file << ",\n    \"vertexInputs\": ";
  WriteVertexInputEntries(file, reflection.vertexInputs);
  file << "\n  }";
}

}  // namespace

bool WriteStageDump(const std::string& dir, const std::vector<VariantData>& variants,
                    const ShaderSourceSet& sources, Diagnostics* diagnostics) {
  std::error_code errorCode;
  const std::filesystem::path root(dir);
  std::filesystem::create_directories(root / "expanded", errorCode);
  for (const auto& shader : sources.shaders) {
    if (!WriteTextFile(root / "expanded" / (shader.info.name + ".vert"), shader.vertex,
                       diagnostics)) {
      return false;
    }
    if (!WriteTextFile(root / "expanded" / (shader.info.name + ".frag"), shader.fragment,
                       diagnostics)) {
      return false;
    }
  }
  std::set<std::string> createdProfiles;
  for (const auto& variant : variants) {
    const std::filesystem::path profileDir = root / variant.profileTag;
    if (createdProfiles.insert(variant.profileTag).second) {
      std::filesystem::create_directories(profileDir, errorCode);
    }
    std::stringstream fileName;
    fileName << variant.shaderName << "_v" << variant.vertPermutationIndex << "_f"
             << variant.fragPermutationIndex << ".json";
    const auto filePath = profileDir / fileName.str();
    std::ofstream file(filePath);
    if (!file.is_open()) {
      ReportWriteFailure(diagnostics, filePath.string());
      return false;
    }
    file << "{\n";
    file << "  \"shader\": \"" << variant.shaderName << "\",\n";
    file << "  \"vertIndex\": " << variant.vertPermutationIndex << ",\n";
    file << "  \"fragIndex\": " << variant.fragPermutationIndex << ",\n";
    file << "  \"profile\": \"" << variant.profileTag << "\",\n";
    WriteStage(file, "vertex", variant.vertexBlob, variant.vertexReflection);
    file << ",\n";
    WriteStage(file, "fragment", variant.fragmentBlob, variant.fragmentReflection);
    file << "\n}\n";
  }
  return true;
}

}  // namespace tgfx

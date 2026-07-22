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

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include "BundleWriter.h"
#include "ReflectionExtractor.h"
#include "ShaderCompiler.h"
#include "gpu/shaders/PrecompiledShader.h"
#include "gpu/shaders/level1/AlphaThresholdShader.h"
#include "gpu/shaders/level1/AtlasTextFillShader.h"
#include "gpu/shaders/level1/BlendMergeShader.h"
#include "gpu/shaders/level1/ColorSpaceXformShader.h"
#include "gpu/shaders/level1/ComplexEllipseFillShader.h"
#include "gpu/shaders/level1/ComplexNonAARRectFillShader.h"
#include "gpu/shaders/level1/ConstColorShader.h"
#include "gpu/shaders/level1/DeviceSpaceTextureShader.h"
#include "gpu/shaders/level1/DualIntervalGradientShader.h"
#include "gpu/shaders/level1/EllipseFillShader.h"
#include "gpu/shaders/level1/GaussianBlur1DShader.h"
#include "gpu/shaders/level1/GradientFillShader.h"
#include "gpu/shaders/level1/HairlineLineShader.h"
#include "gpu/shaders/level1/HairlineQuadShader.h"
#include "gpu/shaders/level1/LumaShader.h"
#include "gpu/shaders/level1/MeshFillShader.h"
#include "gpu/shaders/level1/NonAARRectFillShader.h"
#include "gpu/shaders/level1/QuadColorFillShader.h"
#include "gpu/shaders/level1/QuadTextureFillShader.h"
#include "gpu/shaders/level1/RoundStrokeRectFillShader.h"
#include "gpu/shaders/level1/ShapeInstancedFillShader.h"
#include "gpu/shaders/level1/SingleIntervalGradientShader.h"
#include "gpu/shaders/level1/SolidColorFillShader.h"
#include "gpu/shaders/level1/TextureColorMatrixShader.h"
#include "gpu/shaders/level1/TextureFillShader.h"
#include "gpu/shaders/level1/TextureGradientShader.h"
#include "gpu/shaders/level1/TexturedAlphaThresholdShader.h"
#include "gpu/shaders/level1/TexturedColorMatrixShader.h"
#include "gpu/shaders/level1/TexturedColorSpaceXformShader.h"
#include "gpu/shaders/level1/TexturedLumaShader.h"
#include "gpu/shaders/level1/TiledTextureFillShader.h"

namespace tgfx {

struct BuildOptions {
  std::string shaderDir;
  std::string outDir;
  std::vector<std::string> backends;
  bool reportOnly = false;
  bool compress = false;
};

struct ShaderReport {
  std::string name;
  uint32_t rawCount = 0;
  uint32_t compiledCount = 0;
  uint32_t errorCount = 0;
};

struct BuildReport {
  std::vector<ShaderReport> shaders;
  std::vector<VariantData> variants;
  std::map<std::string, uint64_t> profileErrorCounts;
  bool hasErrors = false;
  std::string errorMessage;
};

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

static std::map<std::string, ProfileArtifactStats> CollectArtifactStats(
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

static void RecordCommonArtifactError(std::map<std::string, uint64_t>* profileErrorCounts) {
  for (auto& profile : *profileErrorCounts) {
    profile.second++;
  }
}

static void RecordBackendArtifactError(const std::string& backend,
                                       std::map<std::string, uint64_t>* profileErrorCounts) {
  (*profileErrorCounts)[backend]++;
}

static void PrintUsage() {
  std::cerr << "Usage: shader_build_tool [options]\n"
            << "  --shader-dir <path>   Directory containing shader sources\n"
            << "  --out-dir <path>      Output directory for build artifacts\n"
            << "  --backends <list>     Comma-separated backend list (opengl,vulkan,metal,webgpu)\n"
            << "  --report-only         Only enumerate and report, do not compile\n"
            << "  --compress            Compress data pool with zlib in output bundles\n";
}

static std::vector<std::string> SplitByComma(const std::string& input) {
  std::vector<std::string> result;
  size_t start = 0;
  while (start < input.size()) {
    auto comma = input.find(',', start);
    if (comma == std::string::npos) {
      comma = input.size();
    }
    auto token = input.substr(start, comma - start);
    if (!token.empty()) {
      result.push_back(token);
    }
    start = comma + 1;
  }
  return result;
}

static bool ParseArgs(int argc, char** argv, BuildOptions* options) {
  for (int i = 1; i < argc; i++) {
    if (std::strcmp(argv[i], "--shader-dir") == 0 && i + 1 < argc) {
      options->shaderDir = argv[++i];
    } else if (std::strcmp(argv[i], "--out-dir") == 0 && i + 1 < argc) {
      options->outDir = argv[++i];
    } else if (std::strcmp(argv[i], "--backends") == 0 && i + 1 < argc) {
      options->backends = SplitByComma(argv[++i]);
    } else if (std::strcmp(argv[i], "--report-only") == 0) {
      options->reportOnly = true;
    } else if (std::strcmp(argv[i], "--compress") == 0) {
      options->compress = true;
    } else {
      std::cerr << "Unknown option: " << argv[i] << "\n";
      PrintUsage();
      return false;
    }
  }
  if (options->outDir.empty()) {
    options->outDir = ".";
  }
  return true;
}

static std::string ReadFileContents(const std::string& path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    return "";
  }
  std::stringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

static std::string ResolveIncludes(const std::string& source, const std::string& baseDir) {
  std::string result;
  std::istringstream stream(source);
  std::string line;
  while (std::getline(stream, line)) {
    auto trimmed = line;
    auto firstNonSpace = trimmed.find_first_not_of(" \t");
    if (firstNonSpace != std::string::npos && trimmed.substr(firstNonSpace, 9) == "#include ") {
      auto quoteStart = trimmed.find('"', firstNonSpace + 9);
      auto quoteEnd = trimmed.find('"', quoteStart + 1);
      if (quoteStart != std::string::npos && quoteEnd != std::string::npos) {
        auto includePath = trimmed.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
        auto fullPath = baseDir + "/" + includePath;
        auto includeContent = ReadFileContents(fullPath);
        if (includeContent.empty()) {
          std::cerr << "  WARNING: Cannot resolve #include \"" << includePath << "\"\n";
        }
        result += includeContent + "\n";
        continue;
      }
    }
    result += line + "\n";
  }
  return result;
}

static ShaderReport CompileOneShader(const PrecompiledShaderInfo& info, const BuildOptions& options,
                                     std::vector<VariantData>* outVariants,
                                     std::map<std::string, uint64_t>* profileErrorCounts) {
  ShaderReport report;
  report.name = info.name;
  auto vertDomain = info.vertDomain;
  auto fragDomain = info.fragDomain;
  report.rawCount = vertDomain.totalCount() * fragDomain.totalCount();
  report.compiledCount = 0;
  report.errorCount = 0;

  std::string vertSource;
  std::string fragSource;
  if (!options.reportOnly && !options.shaderDir.empty()) {
    vertSource = ReadFileContents(options.shaderDir + "/" + info.vertexFile);
    fragSource = ReadFileContents(options.shaderDir + "/" + info.fragmentFile);
    if (vertSource.empty()) {
      std::cerr << "  ERROR: Cannot read vertex file or file is empty: " << info.vertexFile << "\n";
      report.errorCount++;
      RecordCommonArtifactError(profileErrorCounts);
    } else {
      auto vertDir = options.shaderDir + "/" + info.vertexFile;
      vertDir = vertDir.substr(0, vertDir.rfind('/'));
      vertSource = ResolveIncludes(vertSource, vertDir);
    }
    if (fragSource.empty()) {
      std::cerr << "  ERROR: Cannot read fragment file or file is empty: " << info.fragmentFile
                << "\n";
      report.errorCount++;
      RecordCommonArtifactError(profileErrorCounts);
    } else {
      auto fragDir = options.shaderDir + "/" + info.fragmentFile;
      fragDir = fragDir.substr(0, fragDir.rfind('/'));
      fragSource = ResolveIncludes(fragSource, fragDir);
    }
  }

  // Cache compiled vertex shaders by vertPermutationIndex to avoid redundant compilation when
  // multiple frag variants share the same vert variant.
  struct VertCacheEntry {
    std::vector<uint32_t> spirv;
    StageReflectionData reflection;
  };
  std::map<uint32_t, VertCacheEntry> vertCache;

  for (uint32_t vi = 0; vi < vertDomain.totalCount(); vi++) {
    auto vertValues = vertDomain.decode(vi);
    for (uint32_t fi = 0; fi < fragDomain.totalCount(); fi++) {
      auto fragValues = fragDomain.decode(fi);
      if (info.shouldCompile && !info.shouldCompile(vi, fi, vertValues, fragValues)) {
        continue;
      }
      report.compiledCount++;

      if (options.reportOnly || vertSource.empty() || fragSource.empty()) {
        continue;
      }

      auto vertDefines = vertDomain.defineListFor(vi);
      auto fragDefines = fragDomain.defineListFor(fi);

      // Compile vertex shader (use cache if already compiled for this vertIndex)
      std::vector<uint32_t>* vertSpirv = nullptr;
      StageReflectionData* vertReflData = nullptr;
      auto vertIt = vertCache.find(vi);
      if (vertIt != vertCache.end()) {
        vertSpirv = &vertIt->second.spirv;
        vertReflData = &vertIt->second.reflection;
      } else {
        auto expandedVert = PrependDefines(vertSource, vertDefines);
        auto vertResult = CompileGLSL(expandedVert, ShaderStageType::Vertex, info.name, vi);
        if (!vertResult.success) {
          std::cerr << "  " << vertResult.error << "\n";
          report.errorCount++;
          RecordCommonArtifactError(profileErrorCounts);
          continue;
        }
        // Store a dummy reflection for now; we'll fill it after frag compilation
        auto& cacheEntry = vertCache[vi];
        cacheEntry.spirv = std::move(vertResult.spirv);
        vertSpirv = &cacheEntry.spirv;
        vertReflData = &cacheEntry.reflection;
      }

      // Compile fragment shader
      auto expandedFrag = PrependDefines(fragSource, fragDefines);
      auto fragResult = CompileGLSL(expandedFrag, ShaderStageType::Fragment, info.name, fi);
      if (!fragResult.success) {
        std::cerr << "  " << fragResult.error << "\n";
        report.errorCount++;
        RecordCommonArtifactError(profileErrorCounts);
        continue;
      }

      // Extract reflection from SPIR-V
      auto reflection = ExtractReflection(*vertSpirv, fragResult.spirv);
      // Update vert reflection cache on first successful extraction
      if (vertReflData->uniforms.empty() && vertReflData->samplers.empty()) {
        *vertReflData = reflection.vertexReflection;
      }

      for (const auto& backend : options.backends) {
        std::vector<uint8_t> vertBlob;
        std::vector<uint8_t> fragBlob;

        if (backend == "vulkan") {
          // Re-compile with optimization for smaller SPIR-V output.
          auto expandedVertOpt = PrependDefines(vertSource, vertDefines);
          auto vertOpt = CompileGLSL(expandedVertOpt, ShaderStageType::Vertex, info.name, vi, true);
          auto expandedFragOpt = PrependDefines(fragSource, fragDefines);
          auto fragOpt =
              CompileGLSL(expandedFragOpt, ShaderStageType::Fragment, info.name, fi, true);
          if (!vertOpt.success || !fragOpt.success) {
            // Fallback to unoptimized if optimization fails.
            auto* vp = reinterpret_cast<const uint8_t*>(vertSpirv->data());
            vertBlob.assign(vp, vp + vertSpirv->size() * 4);
            auto* fp = reinterpret_cast<const uint8_t*>(fragResult.spirv.data());
            fragBlob.assign(fp, fp + fragResult.spirv.size() * 4);
          } else {
            auto* vp = reinterpret_cast<const uint8_t*>(vertOpt.spirv.data());
            vertBlob.assign(vp, vp + vertOpt.spirv.size() * 4);
            auto* fp = reinterpret_cast<const uint8_t*>(fragOpt.spirv.data());
            fragBlob.assign(fp, fp + fragOpt.spirv.size() * 4);
          }
        } else if (backend == "metal") {
          auto mslVert = TranslateToMSL(*vertSpirv, ShaderStageType::Vertex);
          auto mslFrag = TranslateToMSL(fragResult.spirv, ShaderStageType::Fragment);
          if (!mslVert.success || !mslFrag.success) {
            std::cerr << "  MSL translation error: "
                      << (mslVert.success ? mslFrag.error : mslVert.error) << "\n";
            report.errorCount++;
            RecordBackendArtifactError(backend, profileErrorCounts);
            continue;
          }
          vertBlob = CompileMSLToMetallib(mslVert.msl, ShaderStageType::Vertex);
          fragBlob = CompileMSLToMetallib(mslFrag.msl, ShaderStageType::Fragment);
          if (vertBlob.empty() || fragBlob.empty()) {
            std::cerr << "  metallib compilation failed for " << info.name << " [vert=" << vi
                      << " frag=" << fi << "]\n";
            report.errorCount++;
            RecordBackendArtifactError(backend, profileErrorCounts);
            continue;
          }
        } else if (backend == "webgpu") {
          auto wgslVert = TranslateToWGSL(*vertSpirv);
          auto wgslFrag = TranslateToWGSL(fragResult.spirv);
          if (!wgslVert.success || !wgslFrag.success) {
            std::cerr << "  WGSL translation error: "
                      << (wgslVert.success ? wgslFrag.error : wgslVert.error) << "\n";
            report.errorCount++;
            RecordBackendArtifactError(backend, profileErrorCounts);
            continue;
          }
          vertBlob.assign(wgslVert.wgsl.begin(), wgslVert.wgsl.end());
          fragBlob.assign(wgslFrag.wgsl.begin(), wgslFrag.wgsl.end());
        } else if (backend == "opengl") {
          auto glslVert = PrependDefines(vertSource, vertDefines);
          auto glslFrag = PrependDefines(fragSource, fragDefines);
          vertBlob.assign(glslVert.begin(), glslVert.end());
          fragBlob.assign(glslFrag.begin(), glslFrag.end());
        } else {
          continue;
        }

        VariantData variant;
        variant.shaderName = info.name;
        variant.vertPermutationIndex = vi;
        variant.fragPermutationIndex = fi;
        variant.profileTag = backend;
        variant.vertexBlob = std::move(vertBlob);
        variant.fragmentBlob = std::move(fragBlob);
        variant.vertexReflection = reflection.vertexReflection;
        variant.fragmentReflection = reflection.fragmentReflection;
        outVariants->push_back(std::move(variant));
      }
    }
  }
  return report;
}

static bool WriteReportJson(const BuildReport& report, const std::string& outDir) {
  std::string path = outDir + "/shader_build_report.json";
  std::ofstream file(path);
  if (!file.is_open()) {
    std::cerr << "Failed to open output file: " << path << "\n";
    return false;
  }
  auto artifactStats = CollectArtifactStats(report.variants, report.profileErrorCounts);
  file << "{\n  \"shaders\": [\n";
  for (size_t i = 0; i < report.shaders.size(); i++) {
    const auto& shader = report.shaders[i];
    file << "    {\n";
    file << "      \"name\": \"" << shader.name << "\",\n";
    file << "      \"rawCount\": " << shader.rawCount << ",\n";
    file << "      \"compiledCount\": " << shader.compiledCount << ",\n";
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

int main(int argc, char** argv) {
  tgfx::BuildOptions options;
  if (!tgfx::ParseArgs(argc, argv, &options)) {
    return 1;
  }

  tgfx::BuildReport report;
  for (const auto& backend : options.backends) {
    report.profileErrorCounts.emplace(backend, 0);
  }
  const auto& factories = tgfx::ShaderRegistry::All();
  if (factories.empty()) {
    std::cerr << "No shaders registered in ShaderRegistry.\n";
    return 1;
  }

  uint32_t totalErrors = 0;
  for (const auto& factory : factories) {
    auto shader = factory();
    auto info = shader->info();
    auto shaderReport =
        tgfx::CompileOneShader(info, options, &report.variants, &report.profileErrorCounts);
    std::cout << "[" << shaderReport.name << "] raw=" << shaderReport.rawCount
              << " compiled=" << shaderReport.compiledCount;
    if (shaderReport.errorCount > 0) {
      std::cout << " errors=" << shaderReport.errorCount;
    }
    std::cout << "\n";
    totalErrors += shaderReport.errorCount;
    report.shaders.push_back(std::move(shaderReport));
  }

  if (!tgfx::WriteReportJson(report, options.outDir)) {
    return 1;
  }

  if (totalErrors > 0) {
    std::cerr << "Build failed: " << totalErrors << " shader compilation error(s).\n";
    return 1;
  }

  // Write bundle files grouped by backend (profileTag)
  if (!options.reportOnly && !report.variants.empty()) {
    std::map<std::string, std::vector<tgfx::VariantData>> byBackend;
    for (auto& v : report.variants) {
      byBackend[v.profileTag].push_back(std::move(v));
    }
    for (const auto& pair : byBackend) {
      std::string filename = "shader_bundle." + pair.first + ".bin";
      std::string path = options.outDir + "/" + filename;
      if (!tgfx::WriteBundle(path, pair.first, pair.second, options.compress)) {
        std::cerr << "Failed to write bundle: " << path << "\n";
        return 1;
      }
      std::cout << "Bundle written: " << filename << " (" << pair.second.size() << " entries)\n";
    }
  }

  return 0;
}

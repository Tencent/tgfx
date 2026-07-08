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
#include <sstream>
#include <string>
#include <vector>
#include "ShaderCompiler.h"
#include "gpu/shaders/PrecompiledShader.h"
#include "gpu/shaders/level1/TextureFillShader.h"

namespace tgfx {

struct BuildOptions {
  std::string shaderDir;
  std::string outDir;
  std::vector<std::string> backends;
  bool reportOnly = false;
};

struct ShaderReport {
  std::string name;
  uint32_t rawCount = 0;
  uint32_t compiledCount = 0;
  uint32_t errorCount = 0;
};

struct BuildReport {
  std::vector<ShaderReport> shaders;
  bool hasErrors = false;
  std::string errorMessage;
};

static void PrintUsage() {
  std::cerr << "Usage: shader_build_tool [options]\n"
            << "  --shader-dir <path>   Directory containing shader sources\n"
            << "  --out-dir <path>      Output directory for build artifacts\n"
            << "  --backends <list>     Comma-separated backend list (opengl,vulkan,metal,webgpu)\n"
            << "  --report-only         Only enumerate and report, do not compile\n";
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

static ShaderReport CompileOneShader(const PrecompiledShaderInfo& info,
                                     const BuildOptions& options) {
  ShaderReport report;
  report.name = info.name;
  auto domain = info.domain;
  report.rawCount = domain.totalCount();
  report.compiledCount = 0;
  report.errorCount = 0;

  std::string vertSource;
  std::string fragSource;
  if (!options.reportOnly && !options.shaderDir.empty()) {
    vertSource = ReadFileContents(options.shaderDir + "/" + info.vertexFile);
    fragSource = ReadFileContents(options.shaderDir + "/" + info.fragmentFile);
    if (vertSource.empty()) {
      std::cerr << "  WARNING: Cannot read vertex file: " << info.vertexFile << "\n";
    }
    if (fragSource.empty()) {
      std::cerr << "  WARNING: Cannot read fragment file: " << info.fragmentFile << "\n";
    }
  }

  for (uint32_t i = 0; i < report.rawCount; i++) {
    auto values = domain.decode(i);
    if (info.shouldCompile && !info.shouldCompile(values)) {
      continue;
    }
    report.compiledCount++;

    if (options.reportOnly || vertSource.empty() || fragSource.empty()) {
      continue;
    }

    auto defines = domain.defineListFor(i);
    auto expandedVert = PrependDefines(vertSource, defines);
    auto expandedFrag = PrependDefines(fragSource, defines);

    for (const auto& backend : options.backends) {
      if (backend == "vulkan" || backend == "metal" || backend == "webgpu") {
        auto vertResult = CompileGLSL(expandedVert, ShaderStageType::Vertex, info.name, i);
        if (!vertResult.success) {
          std::cerr << "  " << vertResult.error << "\n";
          report.errorCount++;
          continue;
        }
        auto fragResult = CompileGLSL(expandedFrag, ShaderStageType::Fragment, info.name, i);
        if (!fragResult.success) {
          std::cerr << "  " << fragResult.error << "\n";
          report.errorCount++;
          continue;
        }
        if (backend == "metal") {
          auto mslVert = TranslateToMSL(vertResult.spirv);
          auto mslFrag = TranslateToMSL(fragResult.spirv);
          if (!mslVert.success || !mslFrag.success) {
            std::cerr << "  MSL translation error: "
                      << (mslVert.success ? mslFrag.error : mslVert.error) << "\n";
            report.errorCount++;
          }
        } else if (backend == "webgpu") {
          auto wgslVert = TranslateToWGSL(vertResult.spirv);
          auto wgslFrag = TranslateToWGSL(fragResult.spirv);
          if (!wgslVert.success || !wgslFrag.success) {
            std::cerr << "  WGSL translation error: "
                      << (wgslVert.success ? wgslFrag.error : wgslVert.error) << "\n";
            report.errorCount++;
          }
        }
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
  file << "{\n  \"shaders\": [\n";
  for (size_t i = 0; i < report.shaders.size(); i++) {
    const auto& shader = report.shaders[i];
    file << "    {\n";
    file << "      \"name\": \"" << shader.name << "\",\n";
    file << "      \"rawCount\": " << shader.rawCount << ",\n";
    file << "      \"compiledCount\": " << shader.compiledCount << "\n";
    file << "    }";
    if (i + 1 < report.shaders.size()) {
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
  const auto& factories = tgfx::ShaderRegistry::All();
  if (factories.empty()) {
    std::cerr << "No shaders registered in ShaderRegistry.\n";
    return 1;
  }

  for (const auto& factory : factories) {
    auto shader = factory();
    auto info = shader->info();
    auto shaderReport = tgfx::CompileOneShader(info, options);
    std::cout << "[" << shaderReport.name << "] raw=" << shaderReport.rawCount
              << " compiled=" << shaderReport.compiledCount << "\n";
    report.shaders.push_back(std::move(shaderReport));
  }

  if (!tgfx::WriteReportJson(report, options.outDir)) {
    return 1;
  }

  return 0;
}

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
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include "BundleManifest.h"
#include "BundleVerifier.h"
#include "BundleWriter.h"
#include "ContractChecks.h"
#include "InlineExpansion.h"
#include "ReflectionExtractor.h"
#include "ShaderCompiler.h"
#include "ShaderSources.h"
#include "StageCache.h"
#include "StageReport.h"
#include "compile/StageCompiler.h"
#include "core/Diagnostics.h"
#include "emit/ProfileEmitter.h"
#include "gpu/ShaderProfile.h"
#include "gpu/shaders/PrecompiledShader.h"
#include "gpu/shaders/AllShaderFamilies.h"
#include "plan/VariantPlanner.h"

namespace tgfx {

struct BuildOptions {
  std::string shaderDir;
  std::string outDir;
  std::vector<ShaderProfile> backends;
  bool reportOnly = false;
  bool compress = false;
  bool audit = false;
  std::string stageReportPath;
  std::string inlineReportPath;
  std::string verifyBundleDir;
  // Set by --require-backends; stays empty (nullopt) when the flag is absent.
  std::optional<std::vector<std::string>> requiredBackends;
  // Persistent stage cache. Defaults to <out-dir>/stage_cache; --no-cache keeps it memory-only.
  std::string cacheDir;
  bool noCache = false;
  // The platform the Metal bundle is compiled for (see SetMetalTarget).
  std::string metalSdk = "macosx";
  std::string metalMinOS;
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
  // One per compiled fragment permutation, from its unoptimized SPIR-V.
  std::vector<InlineExpansionRecord> inlineExpansions;
  // The per-profile error ledger; the compile loop records through it and the report reads it.
  Diagnostics diagnostics;
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

static void PrintUsage() {
  std::cerr
      << "Usage: shader_build_tool [options]\n"
      << "  --shader-dir <path>   Directory containing shader sources\n"
      << "  --out-dir <path>      Output directory for build artifacts\n"
      << "  --backends <list>     Comma-separated backend list "
         "(opengl,opengles,vulkan,metal,webgpu)\n"
      << "  --report-only         Only enumerate and report, do not compile\n"
      << "  --audit               Cross-check legacy compile lists against rule-reachable sets\n"
      << "  --stage-report <path> Write the per-stage audit report (requires a full compile)\n"
      << "  --inline-report <path>\n"
      << "                        Write each fragment variant's inline expansion (functions the\n"
      << "                        driver compiles more than once) as JSON\n"
      << "  --verify-bundle <dir> Verify existing bundles in <dir> against the reachable set and\n"
      << "                        exit; checks headers, identity hash, and pool completeness.\n"
      << "                        With --shader-dir it also fails any bundle whose manifest\n"
      << "                        records different sources than the ones in that directory\n"
      << "  --require-backends <list>\n"
      << "                        With --verify-bundle: backends whose bundle must exist; a\n"
      << "                        missing one is a violation. Without it, missing bundles pass\n"
      << "  --compress            Compress data pool with zlib in output bundles\n"
      << "  --cache-dir <path>    Persistent stage cache (default: <out-dir>/stage_cache)\n"
      << "  --no-cache            Compile every stage, reading and writing no persistent cache\n"
      << "  --metal-sdk <sdk>     Platform of the Metal bundle: macosx (default), iphoneos or\n"
      << "                        iphonesimulator. A metallib only works on its own platform\n"
      << "  --metal-min-os <ver>  Minimum OS version of the Metal bundle, e.g. 15.0\n";
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

static bool ParseProfileList(const std::string& input, std::vector<ShaderProfile>* profiles) {
  for (const auto& tag : SplitByComma(input)) {
    auto profile = ParseProfileTag(tag);
    if (!profile) {
      std::cerr << "Unknown backend: " << tag << "\n";
      return false;
    }
    profiles->push_back(*profile);
  }
  return true;
}

static bool ParseArgs(int argc, char** argv, BuildOptions* options) {
  for (int i = 1; i < argc; i++) {
    if (std::strcmp(argv[i], "--shader-dir") == 0 && i + 1 < argc) {
      options->shaderDir = argv[++i];
    } else if (std::strcmp(argv[i], "--out-dir") == 0 && i + 1 < argc) {
      options->outDir = argv[++i];
    } else if (std::strcmp(argv[i], "--backends") == 0 && i + 1 < argc) {
      if (!ParseProfileList(argv[++i], &options->backends)) {
        PrintUsage();
        return false;
      }
    } else if (std::strcmp(argv[i], "--report-only") == 0) {
      options->reportOnly = true;
    } else if (std::strcmp(argv[i], "--audit") == 0) {
      options->audit = true;
    } else if (std::strcmp(argv[i], "--stage-report") == 0 && i + 1 < argc) {
      options->stageReportPath = argv[++i];
    } else if (std::strcmp(argv[i], "--inline-report") == 0 && i + 1 < argc) {
      options->inlineReportPath = argv[++i];
    } else if (std::strcmp(argv[i], "--verify-bundle") == 0 && i + 1 < argc) {
      options->verifyBundleDir = argv[++i];
    } else if (std::strcmp(argv[i], "--require-backends") == 0 && i + 1 < argc) {
      options->requiredBackends = SplitByComma(argv[++i]);
    } else if (std::strcmp(argv[i], "--compress") == 0) {
      options->compress = true;
    } else if (std::strcmp(argv[i], "--cache-dir") == 0 && i + 1 < argc) {
      options->cacheDir = argv[++i];
    } else if (std::strcmp(argv[i], "--no-cache") == 0) {
      options->noCache = true;
    } else if (std::strcmp(argv[i], "--metal-sdk") == 0 && i + 1 < argc) {
      options->metalSdk = argv[++i];
    } else if (std::strcmp(argv[i], "--metal-min-os") == 0 && i + 1 < argc) {
      options->metalMinOS = argv[++i];
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

// source is the shader's text from the build's single read of the sources (nullptr in report-only
// mode); the bundle's source digest is computed from the same text. compiler owns the stage
// caches of this build.
static ShaderReport CompileOneShader(const PrecompiledShaderInfo& info, const BuildOptions& options,
                                     const ShaderSource* source, StageCompiler* compiler,
                                     std::vector<VariantData>* outVariants,
                                     std::vector<InlineExpansionRecord>* outInlineExpansions,
                                     Diagnostics* diagnostics) {
  ShaderReport report;
  std::set<uint32_t> analyzedFrags;
  report.name = info.name;
  auto vertDomain = info.vertDomain;
  auto fragDomain = info.fragDomain;
  report.rawCount = vertDomain.totalCount() * fragDomain.totalCount();
  report.compiledCount = 0;
  report.errorCount = 0;

  // One emitter per requested backend, built once for the whole shader: the loop below only
  // feeds them variants.
  std::vector<std::pair<ShaderProfile, std::unique_ptr<ProfileEmitter>>> emitters;
  for (auto profile : options.backends) {
    emitters.emplace_back(profile, MakeProfileEmitter(profile));
  }

  // Unreadable files and broken includes were rejected when the sources were loaded, so a
  // present source is complete.
  std::string vertSource = source != nullptr ? source->vertex : "";
  std::string fragSource = source != nullptr ? source->fragment : "";

  // The variant plan of this family: the Compose-reachable set (the single source of truth the
  // runtime matcher uses too), validated against the declared domains. The build loop,
  // --audit and --report-only all get their variants from the planner.
  auto plan = VariantPlanner::PlanShader(info);
  if (!plan.variants) {
    std::cerr << "  ERROR: no rule enumerator for " << info.name
              << ": every shader must be migrated to the Compose pattern\n";
    report.errorCount++;
    diagnostics->recordCommonError();
    return report;
  }
  if (!plan.unbuildable.empty()) {
    // A pair the domains cannot serve would compile against a broken vertex/fragment
    // interface; fail the family closed instead of emitting it.
    for (const auto& pair : plan.unbuildable) {
      std::cerr << "  ERROR: unbuildable variant vert=" << pair.first << " frag=" << pair.second
                << " for " << info.name << "\n";
    }
    report.errorCount++;
    diagnostics->recordCommonError();
    return report;
  }
  for (const auto& permutation : *plan.variants) {
    uint32_t vi = permutation.first;
    uint32_t fi = permutation.second;

    if (options.reportOnly || vertSource.empty() || fragSource.empty()) {
      continue;
    }

    auto vertDefines = vertDomain.defineListFor(vi);
    auto fragDefines = fragDomain.defineListFor(fi);

    // Compile vertex shader. The shared cache also covers the multiple-frag-per-vert reuse the
    // old per-backend vertCache handled, plus the cross-backend repeats.
    const auto& vertResult = compiler->compileSpirv(PrependDefines(vertSource, vertDefines),
                                                    ShaderStageType::Vertex, info.name, vi, false);
    if (!vertResult.success) {
      std::cerr << "  " << vertResult.error << "\n";
      report.errorCount++;
      diagnostics->recordCommonError();
      continue;
    }
    const auto* vertSpirv = &vertResult.spirv;

    const auto& fragResult = compiler->compileSpirv(PrependDefines(fragSource, fragDefines),
                                                    ShaderStageType::Fragment, info.name, fi,
                                                    false);
    if (!fragResult.success) {
      std::cerr << "  " << fragResult.error << "\n";
      report.errorCount++;
      diagnostics->recordCommonError();
      continue;
    }

    // The unoptimized SPIR-V still has one function per GLSL function and one OpFunctionCall per
    // call site, so it shows what every backend's driver ends up inlining.
    if (analyzedFrags.insert(fi).second) {
      outInlineExpansions->push_back({info.name, fi, AnalyzeInlineExpansion(fragResult.spirv)});
    }

    // Extract reflection from SPIR-V (shared across backend passes, like the compiles above).
    const auto& reflection =
        compiler->extractReflection(info.name, vi, fi, *vertSpirv, fragResult.spirv);
    // Counted only here — after both stages compiled successfully — so compiledCount is the
    // true "compiled" figure, distinct from rawCount (the reachable-permutation total) and
    // independent of backend-level exclusions counted below.
    report.compiledCount++;

    for (auto& emitterEntry : emitters) {
      const ShaderProfile profile = emitterEntry.first;
      const std::string backend = ProfileTag(profile);
      // Profile-specific exclusions (the framebuffer fetch variants) share one source of truth
      // with the closure verifier; see PermutationCompilesForBackend.
      if (!PermutationCompilesForBackend(info, vi, fi, profile)) {
        continue;
      }
      EmitInput input;
      input.info = &info;
      input.vertIndex = vi;
      input.fragIndex = fi;
      input.vertSource = &vertSource;
      input.fragSource = &fragSource;
      input.vertDefines = &vertDefines;
      input.fragDefines = &fragDefines;
      input.vertSpirv = vertSpirv;
      input.fragStage = &fragResult;
      auto stages = emitterEntry.second->emit(input, compiler);
      if (!stages) {
        // The emitter printed why; this is the single place the failure is counted.
        report.errorCount++;
        diagnostics->recordProfileError(backend);
        continue;
      }
      VariantData variant;
      variant.shaderName = info.name;
      variant.vertPermutationIndex = vi;
      variant.fragPermutationIndex = fi;
      variant.profileTag = backend;
      variant.vertexBlob = std::move(stages->vertexBlob);
      variant.fragmentBlob = std::move(stages->fragmentBlob);
      variant.vertexReflection = reflection.vertexReflection;
      variant.fragmentReflection = reflection.fragmentReflection;
      outVariants->push_back(std::move(variant));
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

// Validates every shader's rule-reachable set: each (vertIndex, fragIndex) pair the Compose
// functions can produce must be a structurally valid permutation (indices within the declared
// domains and mirrored dimensions in agreement), because the compile list is derived directly
// from these sets. An invalid entry would compile a variant with a broken vertex/fragment
// interface, so any violation fails the audit. The audit asks the same planner the build loop
// does, so the two paths cannot disagree.
static int RunAuditMode() {
  const auto& factories = ShaderRegistry::All();
  size_t audited = 0;
  size_t violations = 0;
  for (const auto& factory : factories) {
    auto shader = factory();
    auto info = shader->info();
    auto plan = VariantPlanner::PlanShader(info);
    if (!plan.variants) {
      std::cerr << "[audit] " << info.name << ": ERROR no rule enumerator\n";
      violations++;
      continue;
    }
    audited++;
    for (const auto& pair : plan.unbuildable) {
      violations++;
      std::cout << "[audit] " << info.name << ": INVALID vert=" << pair.first
                << " frag=" << pair.second << " (out of range or mirrored dimensions disagree)\n";
    }
    if (plan.unbuildable.empty()) {
      std::cout << "[audit] " << info.name << ": OK (" << plan.variants->size()
                << " reachable permutations)\n";
    }
  }
  std::cout << "[audit] summary: " << audited << " audited, " << violations << " violations\n";
  return violations == 0 ? 0 : 1;
}

}  // namespace tgfx

int main(int argc, char** argv) {
  tgfx::BuildOptions options;
  if (!tgfx::ParseArgs(argc, argv, &options)) {
    return 1;
  }
  std::string targetError;
  if (!tgfx::SetMetalTarget(options.metalSdk, options.metalMinOS, &targetError)) {
    std::cerr << "ERROR: " << targetError << "\n";
    return 1;
  }

  if (options.audit) {
    return tgfx::RunAuditMode();
  }

  if (!options.verifyBundleDir.empty()) {
    return tgfx::VerifyBundles(options.verifyBundleDir, options.shaderDir,
                               options.requiredBackends);
  }

  tgfx::BuildReport report;
  for (auto profile : options.backends) {
    report.diagnostics.registerProfile(ProfileTag(profile));
  }
  const auto& factories = tgfx::ShaderRegistry::All();
  if (factories.empty()) {
    std::cerr << "No shaders registered in ShaderRegistry.\n";
    return 1;
  }

  // Every shader file is read exactly once, here. The compile loop and the manifests' source
  // digests both use this set, so a manifest always describes the text that was compiled.
  tgfx::ShaderSourceSet sources;
  const bool compiling = !options.reportOnly && !options.shaderDir.empty();
  if (compiling) {
    sources = tgfx::LoadShaderSources(options.shaderDir);
    if (!sources.ok) {
      for (const auto& error : sources.errors) {
        std::cerr << "  ERROR: " << error << "\n";
      }
      std::cerr << "Build failed: " << sources.errors.size() << " shader source error(s) in "
                << options.shaderDir << ".\n";
      return 1;
    }
  }

  tgfx::StageCache stageCache;
  if (compiling && !options.noCache) {
    auto cacheDir = options.cacheDir.empty() ? options.outDir + "/stage_cache" : options.cacheDir;
    std::string cacheError;
    if (!stageCache.open(cacheDir, &cacheError)) {
      std::cerr << "  WARNING: stage cache disabled, compiling every stage: " << cacheError << "\n";
    }
  }
  tgfx::StageCompiler stageCompiler(&stageCache);

  uint32_t totalErrors = 0;
  for (const auto& factory : factories) {
    auto shader = factory();
    auto info = shader->info();
    const tgfx::ShaderSource* source = compiling ? sources.find(info.name) : nullptr;
    if (compiling && source == nullptr) {
      std::cerr << "  ERROR: no loaded source for " << info.name << "\n";
      return 1;
    }
    auto shaderReport = tgfx::CompileOneShader(info, options, source, &stageCompiler,
                                               &report.variants, &report.inlineExpansions,
                                               &report.diagnostics);
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

  // Must run before the variants are moved into the per-backend bundles below.
  if (!options.stageReportPath.empty() && !report.variants.empty()) {
    if (!tgfx::WriteStageReport(options.stageReportPath, report.variants)) {
      return 1;
    }
  }

  // Cross-language contract checks: the clip contract fields and the chain op codes must match
  // the C++ constants, otherwise the runtime uploads would silently miss their targets.
  size_t contractErrors = tgfx::ValidateClipContractFields(report.variants);
  contractErrors += tgfx::ValidateReflectionContracts(report.variants);
  // A heavy function inlined at several call sites multiplies the driver's compile work on every
  // backend that compiles at runtime (GL source, Metal AIR, SPIR-V).
  if (!options.inlineReportPath.empty() &&
      !tgfx::WriteInlineExpansionReport(options.inlineReportPath, report.inlineExpansions)) {
    return 1;
  }
  contractErrors += tgfx::ValidateInlineExpansion(report.inlineExpansions);
  if (!options.reportOnly && !options.shaderDir.empty()) {
    contractErrors += tgfx::ValidateChainOpCodes(options.shaderDir);
    contractErrors += tgfx::ValidatePointwiseOpCodes(options.shaderDir);
    contractErrors += tgfx::ValidateSlotCapacities(options.shaderDir);
    std::vector<std::string> registeredShaders;
    for (auto factory : tgfx::ShaderRegistry::All()) {
      registeredShaders.push_back(factory()->info().name);
    }
    contractErrors += tgfx::ValidateShaderSourceReferences(options.shaderDir, registeredShaders);
  }
  if (contractErrors > 0) {
    std::cerr << "Build failed: " << contractErrors << " kernel contract violation(s).\n";
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
      // The manifest ties the bundle to the exact sources it was built from; --verify-bundle
      // recomputes the digest to tell a stale bundle from a fresh one.
      auto profile = tgfx::ParseProfileTag(pair.first);
      if (!profile) {
        std::cerr << "Cannot write the manifest of " << filename << ": unknown profile "
                  << pair.first << "\n";
        return 1;
      }
      auto digest = tgfx::ComputeSourceDigest(sources, *profile);
      std::string manifestError;
      if (!digest.ok || !tgfx::WriteBundleManifest(path, *profile, digest, &manifestError)) {
        std::cerr << "Failed to write the manifest of " << filename << ": "
                  << (digest.ok ? manifestError : digest.error) << "\n";
        return 1;
      }
    }
  }

  if (compiling) {
    stageCache.printSummary();
  }
  return 0;
}

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

#include "BuildCommand.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <utility>
#include <vector>
#include "output/BundleManifest.h"
#include "output/BundleWriter.h"
#include "InlineExpansion.h"
#include "ShaderCompiler.h"
#include "ShaderSources.h"
#include "compile/ReflectionExtractor.h"
#include "compile/StageCache.h"
#include "output/StageReport.h"
#include "cli/ReportWriter.h"
#include "compile/StageCompiler.h"
#include "core/Diagnostics.h"
#include "emit/ProfileEmitter.h"
#include "gpu/ShaderBindingLayout.h"
#include "gpu/shaders/AllShaderFamilies.h"
#include "gpu/shaders/PrecompiledShader.h"
#include "plan/VariantPlanner.h"
#include "verify/ContractRules.h"

namespace tgfx {

namespace {

void PrintUsage() {
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

std::vector<std::string> SplitByComma(const std::string& input) {
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

bool ParseProfileList(const std::string& input, std::vector<ShaderProfile>* profiles) {
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

// source is the shader's text from the build's single read of the sources (nullptr in report-only
// mode); the bundle's source digest is computed from the same text. compiler owns the stage
// caches of this build.
ShaderReport CompileOneShader(const PrecompiledShaderInfo& info, const BuildOptions& options,
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
    // The binding contract rides in as defines: the templates spell their layout qualifiers
    // with the TGFX_* macro names, so the numbers exist only in ShaderBindingLayout.h. The
    // source digest covers the same list, so a value change invalidates every bundle.
    for (const auto& contractDefine : BindingContractDefines()) {
      vertDefines.push_back(contractDefine);
      fragDefines.push_back(contractDefine);
    }

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

}  // namespace

bool ParseArgs(int argc, char** argv, BuildOptions* options) {
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

int RunBuildCommand(const BuildOptions& options) {
  BuildReport report;
  for (auto profile : options.backends) {
    report.diagnostics.registerProfile(ProfileTag(profile));
  }
  const auto& factories = ShaderRegistry::All();
  if (factories.empty()) {
    std::cerr << "No shaders registered in ShaderRegistry.\n";
    return 1;
  }

  // Every shader file is read exactly once, here. The compile loop and the manifests' source
  // digests both use this set, so a manifest always describes the text that was compiled.
  ShaderSourceSet sources;
  const bool compiling = !options.reportOnly && !options.shaderDir.empty();
  if (compiling) {
    sources = LoadShaderSources(options.shaderDir);
    if (!sources.ok) {
      for (const auto& error : sources.errors) {
        std::cerr << "  ERROR: " << error << "\n";
      }
      std::cerr << "Build failed: " << sources.errors.size() << " shader source error(s) in "
                << options.shaderDir << ".\n";
      return 1;
    }
  }

  StageCache stageCache;
  if (compiling && !options.noCache) {
    auto cacheDir = options.cacheDir.empty() ? options.outDir + "/stage_cache" : options.cacheDir;
    std::string cacheError;
    if (!stageCache.open(cacheDir, &cacheError)) {
      std::cerr << "  WARNING: stage cache disabled, compiling every stage: " << cacheError << "\n";
    }
  }
  StageCompiler stageCompiler(&stageCache);

  uint32_t totalErrors = 0;
  for (const auto& factory : factories) {
    auto shader = factory();
    auto info = shader->info();
    const ShaderSource* source = compiling ? sources.find(info.name) : nullptr;
    if (compiling && source == nullptr) {
      std::cerr << "  ERROR: no loaded source for " << info.name << "\n";
      return 1;
    }
    auto shaderReport = CompileOneShader(info, options, source, &stageCompiler, &report.variants,
                                         &report.inlineExpansions, &report.diagnostics);
    std::cout << "[" << shaderReport.name << "] raw=" << shaderReport.rawCount
              << " compiled=" << shaderReport.compiledCount;
    if (shaderReport.errorCount > 0) {
      std::cout << " errors=" << shaderReport.errorCount;
    }
    std::cout << "\n";
    totalErrors += shaderReport.errorCount;
    report.shaders.push_back(std::move(shaderReport));
  }

  if (!WriteReportJson(report, options.outDir)) {
    return 1;
  }

  // Must run before the variants are moved into the per-backend bundles below.
  if (!options.stageReportPath.empty() && !report.variants.empty()) {
    if (!WriteStageReport(options.stageReportPath, report.variants)) {
      return 1;
    }
  }

  // Cross-language contract checks, as two rule registries: the artifact rules read what the
  // build produced (the clip contract fields, the reflection contracts, the inline expansion
  // policy), the source rules read the shader text (the op codes, the slot capacities, the
  // source references). A rule prints each violation itself; the counts gate the build, so a
  // mismatch fails here instead of surfacing as a runtime upload missing its target.
  if (!options.inlineReportPath.empty() &&
      !WriteInlineExpansionReport(options.inlineReportPath, report.inlineExpansions)) {
    return 1;
  }
  size_t contractErrors = 0;
  ArtifactRuleContext artifactContext;
  artifactContext.variants = &report.variants;
  artifactContext.inlineExpansions = &report.inlineExpansions;
  for (const auto& rule : MakeArtifactRules()) {
    contractErrors += rule->check(artifactContext);
  }
  if (!options.reportOnly && !options.shaderDir.empty()) {
    SourceRuleContext sourceContext;
    sourceContext.shaderDir = options.shaderDir;
    for (auto factory : ShaderRegistry::All()) {
      sourceContext.registeredShaders.push_back(factory()->info().name);
    }
    for (const auto& rule : MakeSourceRules()) {
      contractErrors += rule->check(sourceContext);
    }
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
    std::map<std::string, std::vector<VariantData>> byBackend;
    for (auto& v : report.variants) {
      byBackend[v.profileTag].push_back(std::move(v));
    }
    for (const auto& pair : byBackend) {
      std::string filename = "shader_bundle." + pair.first + ".bin";
      std::string path = options.outDir + "/" + filename;
      if (!WriteBundle(path, pair.first, pair.second, options.compress)) {
        std::cerr << "Failed to write bundle: " << path << "\n";
        return 1;
      }
      std::cout << "Bundle written: " << filename << " (" << pair.second.size() << " entries)\n";
      // The manifest ties the bundle to the exact sources it was built from; --verify-bundle
      // recomputes the digest to tell a stale bundle from a fresh one.
      auto profile = ParseProfileTag(pair.first);
      if (!profile) {
        std::cerr << "Cannot write the manifest of " << filename << ": unknown profile "
                  << pair.first << "\n";
        return 1;
      }
      auto digest = ComputeSourceDigest(sources, *profile);
      std::string manifestError;
      if (!digest.ok || !WriteBundleManifest(path, *profile, digest, &manifestError)) {
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

}  // namespace tgfx

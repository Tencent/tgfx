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

#include "core/CapacityDefines.h"

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <utility>
#include <vector>
#include "output/BundleWriter.h"
#include "compile/InlineExpansion.h"
#include "compile/ShaderCompiler.h"
#include "compile/SpecCodegen.h"
#include "core/ShaderSources.h"
#include "gpu/shaders/contract/SpecContract.h"
#include "compile/ReflectionExtractor.h"
#include "compile/StageCache.h"
#include "output/InlineExpansionReport.h"
#include "output/StageDump.h"
#include "output/StageReport.h"
#include "cli/ReportWriter.h"
#include "compile/StageCompiler.h"
#include "core/DiagnosticCodes.h"
#include "core/Diagnostics.h"
#include "emit/Assembler.h"
#include "gpu/shaders/FamilyDomains.h"
#include "gpu/shaders/Layout.h"
#include "emit/ChainOpsGLSL.h"
#include "emit/ProfileEmitter.h"
#include "gpu/shaders/contract/ShaderBundleContract.h"
#include "gpu/shaders/KernelContract.h"
#include "gpu/shaders/PrecompiledShader.h"
#include "verify/ContractRules.h"

namespace tgfx {

namespace {

/// The SpecId decorations the compiled fragment module carries (OpDecorate with the SpecId
/// decoration). The declared families generate their constants from DeriveSpecFields; this reads
/// back what the compiler actually saw, so a table that drifts from the text fails the build
/// instead of silently specializing nothing.
std::set<uint32_t> SpirvSpecIds(const std::vector<uint32_t>& spirv) {
  std::set<uint32_t> ids = {};
  size_t offset = 5;  // the SPIR-V header
  while (offset < spirv.size()) {
    const uint32_t word = spirv[offset];
    const uint32_t opcode = word & 0xFFFFu;
    const uint32_t wordCount = word >> 16;
    if (wordCount == 0 || offset + wordCount > spirv.size()) {
      break;
    }
    constexpr uint32_t OpDecorate = 71;
    constexpr uint32_t DecorationSpecId = 1;
    // OpDecorate: [target, decoration, literal...] after the opcode word — the SpecId literal
    // sits in the first literal slot, not the target's id.
    if (opcode == OpDecorate && wordCount >= 4 && spirv[offset + 2] == DecorationSpecId) {
      ids.insert(spirv[offset + 3]);
    }
    offset += wordCount;
  }
  return ids;
}


void PrintUsage() {
  std::cerr
      << "Usage: shader_build_tool [options]\n"
      << "  --shader-dir <path>   Directory containing shader sources\n"
      << "  --out-dir <path>      Output directory for build artifacts\n"
      << "  --backends <list>     Comma-separated backend list "
         "(opengl,opengles,vulkan,metal,webgpu,d3d12)\n"

      << "  --stage-report <path> Write the per-stage audit report (requires a full compile)\n"
      << "  --dump-stages <dir>  Write the per-variant stage dump (code hashes, reflection, and\n"
      << "                        the expanded include text) two builds are compared with\n"
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
      << "  --metal-min-os <ver>  Minimum OS version of the Metal bundle, e.g. 15.0\n"
      << "  --d3d12-shader-format <form>\n"
      << "                        What a d3d12 bundle stores: dxbc (default, bytecode compiled\n"
      << "                        here) or hlsl (source the backend compiles at module creation)\n";
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

// Reports one build error against the thing it happened to. The build loop keeps running after a
// failure so one pass surfaces every problem; the recorded errors gate the artifacts at the end.
static void RecordBuildError(Diagnostics* diagnostics, Layer layer, const char* code,
                             const std::string& shader, const std::string& profile, int vert,
                             int frag, std::string message) {
                             Diagnostic diagnostic;
                             diagnostic.severity = Severity::Error;
                             diagnostic.layer = layer;
                             diagnostic.code = code;
                             diagnostic.shader = shader;
                             diagnostic.profile = profile;
                             diagnostic.vertPermutationIndex = vert;
                             diagnostic.fragPermutationIndex = frag;
                             diagnostic.message = std::move(message);
                             diagnostics->report(std::move(diagnostic));
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
    } else if (std::strcmp(argv[i], "--stage-report") == 0 && i + 1 < argc) {
      options->stageReportPath = argv[++i];
    } else if (std::strcmp(argv[i], "--dump-stages") == 0 && i + 1 < argc) {
      options->stageDumpDir = argv[++i];
    } else if (std::strcmp(argv[i], "--inline-report") == 0 && i + 1 < argc) {
      options->inlineReportPath = argv[++i];
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
    } else if (std::strcmp(argv[i], "--d3d12-shader-format") == 0 && i + 1 < argc) {
      options->d3d12ShaderFormat = argv[++i];
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

// The declaration-driven families. Each variant assembles to a self-contained text (locations,
// bindings and block members are that variant's constants), compiles through the same stage
// caches, and enters the bundle under its ordinal — the same mixed-radix index the runtime's
// Select produces, so the bundle keys need no transitional scheme.
namespace {

/// The layout's view of one block's fields, in declaration order, against the reflection's.
bool BlockMatchesReflection(const Layout& layout, uint8_t block,
                            const std::vector<UniformEntry>& reflected) {
  size_t index = 0;
  for (size_t field = 0; field < layout.fields.size(); ++field) {
    if (layout.fields[field].block != block) {
      continue;
    }
    if (index >= reflected.size()) {
      return false;
    }
    if (reflected[index].name != layout.fieldNames[field] ||
        reflected[index].format != static_cast<uint8_t>(layout.fields[field].format) ||
        reflected[index].arraySize != layout.fields[field].arraySize) {
      return false;
    }
    index++;
  }
  return index == reflected.size();
}

/// The only interface check: the reflection of the assembled text must equal the derived layout
/// (uniforms per block, samplers, vertex inputs), or the declaration and the assembly disagree.
bool LayoutMatchesReflection(const Layout& layout, const ReflectionResult& reflection) {
  if (!BlockMatchesReflection(layout, 0, reflection.vertexReflection.uniforms) ||
      !BlockMatchesReflection(layout, 1, reflection.fragmentReflection.uniforms)) {
    return false;
  }
  if (reflection.vertexReflection.samplers.size() != 0 ||
      reflection.fragmentReflection.samplers.size() != layout.textures.size()) {
    return false;
  }
  for (size_t index = 0; index < layout.textures.size(); ++index) {
    if (reflection.fragmentReflection.samplers[index].name != layout.textures[index].name) {
      return false;
    }
  }
  if (reflection.vertexReflection.vertexInputs.size() != layout.attributes.size()) {
    return false;
  }
  for (size_t index = 0; index < layout.attributes.size(); ++index) {
    const auto& attribute = layout.attributes[index];
    const auto& input = reflection.vertexReflection.vertexInputs[index];
    if (input.name != attribute.name || input.location != attribute.location ||
        input.format != static_cast<uint8_t>(attribute.format)) {
      return false;
    }
  }
  return true;
}

}  // namespace

void CompileAssembledFamilies(const BuildOptions& options, StageCompiler* compiler,
                              BuildReport* report) {
  if (options.shaderDir.empty()) {
    return;
  }
  std::vector<std::pair<ShaderProfile, std::unique_ptr<ProfileEmitter>>> emitters;
  for (auto profile : options.backends) {
    emitters.emplace_back(profile, MakeProfileEmitter(profile));
  }
  for (size_t familyIndex = 0; familyIndex < FamilyCount(); ++familyIndex) {
    const Family& family = Families()[familyIndex];
    const auto info = AssembledFamilyInfo(family);

    ShaderReport shaderReport;
    shaderReport.name = family.name;
    shaderReport.rawCount = 0;
    shaderReport.compiledCount = 0;
    shaderReport.errorCount = 0;
    const auto variants = EnumerateVariants(family, static_cast<uint16_t>(familyIndex));
    shaderReport.rawCount = static_cast<uint32_t>(variants.size());
    // A module carrying an unroll site is the interpreter assembly: its shared text reads the
    // operator table (OP_* values) and the slot-pool capacities, which exist only as C++ constants.
    // Those defines are injected for that family alone, so every other family's emitted text keeps
    // its exact bytes.
    bool familyNeedsChainContract = false;
    for (uint8_t moduleIndex = 0; moduleIndex < family.moduleCount; ++moduleIndex) {
      const FamilyModule& module = family.modules[moduleIndex];
      if (module.module != nullptr && module.module->chainContract) {
        familyNeedsChainContract = true;
      }
    }
    for (const auto& variant : variants) {
      const auto assembled = AssembleVariant(family, variant);
      // The assembled text carries module #includes; the compiler never sees one, so the same
      // expansion the handwritten files get runs here first.
      std::string expandError;
      const auto vertText = ExpandStageIncludes(assembled.vertText, options.shaderDir, &expandError);
      const auto fragText = ExpandStageIncludes(assembled.fragText, options.shaderDir, &expandError);
      // The interpreter assembly carries the chain-ops pragma the legacy kernels also use; the
      // declared path runs the same expansion after includes so both assemblies compile the same
      // operator table.
      auto vertExpanded = vertText.empty() ? std::string() : ExpandChainOpsPragma(vertText);
      auto fragExpanded = fragText.empty() ? std::string() : ExpandChainOpsPragma(fragText);
      if (vertExpanded.empty() || fragExpanded.empty()) {
        RecordBuildError(&report->diagnostics, Layer::Sources, DiagnosticCode::SourcesUnreadable,
                         info.name, "", static_cast<int>(variant.ordinal), -1,
                         expandError.empty() ? "include expansion failed" : expandError);
        continue;
      }
      auto vertDefines = assembled.vertDefines;
      auto fragDefines = assembled.fragDefines;
      for (const auto& contractDefine : BindingContractDefines()) {
        vertDefines.push_back(contractDefine);
        fragDefines.push_back(contractDefine);
      }
      if (familyNeedsChainContract) {
        for (const auto& opDefine : Op::Defines()) {
          vertDefines.push_back(opDefine);
          fragDefines.push_back(opDefine);
        }
        for (const auto& capacityDefine : SlotCapacityDefines()) {
          vertDefines.push_back(capacityDefine);
          fragDefines.push_back(capacityDefine);
        }
      }
      const auto& vertResult =
          compiler->compileSpirv(PrependDefines(vertExpanded, vertDefines), ShaderStageType::Vertex,
                                 info.name, variant.ordinal, false);
      if (!vertResult.success) {
        RecordBuildError(&report->diagnostics, Layer::Compile, DiagnosticCode::CompileGlsl,
                         info.name, "", static_cast<int>(variant.ordinal), -1, vertResult.error);
        continue;
      }
      const auto& fragResult =
          compiler->compileSpirv(PrependDefines(fragExpanded, fragDefines),
                                 ShaderStageType::Fragment, info.name, variant.ordinal, false);
      if (!fragResult.success) {
        RecordBuildError(&report->diagnostics, Layer::Compile, DiagnosticCode::CompileGlsl,
                         info.name, "", -1, static_cast<int>(variant.ordinal), fragResult.error);
        continue;
      }
      const auto& reflection =
          compiler->extractReflection(info.name, variant.ordinal, variant.ordinal,
                                      vertResult.spirv, fragResult.spirv, &report->diagnostics);
      const auto layout = DeriveLayout(variant);
      if (!layout.specFields.empty()) {
        // The declared spec ids must be exactly the ids the compiled module carries; a drift
        // would leave the runtime injecting constants the text does not read (the pixels stay
        // right, the specialization silently stops).
        std::set<uint32_t> expected = {0};
        const auto modules = ModulesOf(family, variant);
        for (const auto& field : layout.specFields) {
          const Module* module = modules.list[field.slot];
          const Element& element = module->elements[field.element];
          const uint16_t count =
              ElementCountOf(element, DimOffsetOf(family, variant, field.slot), variant);
          const uint32_t idCount = element.type == ShaderType::Int4 ? count * 4u : 1u;
          for (uint32_t offset = 0; offset < idCount; ++offset) {
            expected.insert(field.firstId + offset);
          }
        }
        if (SpirvSpecIds(fragResult.spirv) != expected) {
          std::string detail = "expected:";
          for (uint32_t id : expected) detail += " " + std::to_string(id);
          detail += " actual:";
          for (uint32_t id : SpirvSpecIds(fragResult.spirv)) detail += " " + std::to_string(id);
          // SpecLayoutDrift is the code the retired tool-side drift check reported; this is the
          // declared path's replacement for exactly that failure mode.
          RecordBuildError(&report->diagnostics, Layer::Emit, DiagnosticCode::SpecLayoutDrift,
                           info.name, "", static_cast<int>(variant.ordinal),
                           static_cast<int>(variant.ordinal),
                           "the declared spec ids and the compiled constant_id set disagree: " +
                               detail);
          continue;
        }
      }
      if (!LayoutMatchesReflection(layout, reflection)) {
        RecordBuildError(&report->diagnostics, Layer::Emit, DiagnosticCode::EmitFailed, info.name,
                         "", static_cast<int>(variant.ordinal), static_cast<int>(variant.ordinal),
                         "the assembled text's reflection does not match the derived layout");
        continue;
      }
      shaderReport.compiledCount++;
      // The specialization table, derived from the declaration like the generated text was: the
      // same DeriveSpecFields call feeds the accessor ids the text carries, so the table the
      // runtime injects through cannot disagree with the constants the shader reads.
      std::vector<SpecEntry> specEntries;
      const auto specFields = DeriveSpecFields(family, variant);
      if (!specFields.empty()) {
        const auto modules = ModulesOf(family, variant);
        for (const auto& field : specFields) {
          const Module* module = modules.list[field.slot];
          const Element& element = module->elements[field.element];
          SpecEntry entry = {};
          entry.name = ElementNameOf(module, element);
          entry.format = static_cast<uint8_t>(ShaderTypeFormat(element.type));
          entry.arraySize =
              element.type == ShaderType::Int4
                  ? ElementCountOf(element, DimOffsetOf(family, variant, field.slot), variant)
                  : 1;
          entry.firstId = field.firstId;
          specEntries.push_back(std::move(entry));
        }
      }
      for (auto& emitterEntry : emitters) {
        if (!PermutationCompilesForBackend(info, variant.ordinal, variant.ordinal,
                                           emitterEntry.first)) {
          continue;
        }
        EmitInput input;
        input.info = &info;
        input.vertIndex = variant.ordinal;
        input.fragIndex = variant.ordinal;
        input.vertSource = &vertExpanded;
        input.fragSource = &fragExpanded;
        input.vertDefines = &vertDefines;
        input.fragDefines = &fragDefines;
        input.vertSpirv = &vertResult.spirv;
        input.fragStage = &fragResult;
        input.diagnostics = &report->diagnostics;
        auto stages = emitterEntry.second->emit(input, compiler);
        if (!stages) {
          continue;
        }
        VariantData variantData;
        variantData.shaderName = info.name;
        variantData.variantId = VariantIdOf(static_cast<uint16_t>(familyIndex), variant.ordinal);
        variantData.vertPermutationIndex = variant.ordinal;
        variantData.fragPermutationIndex = variant.ordinal;
        variantData.profileTag = ProfileTag(emitterEntry.first);
        variantData.payloadKind = stages->payloadKind;
        variantData.vertexBlob = std::move(stages->vertexBlob);
        variantData.fragmentBlob = std::move(stages->fragmentBlob);
        variantData.vertexReflection = reflection.vertexReflection;
        variantData.fragmentReflection = reflection.fragmentReflection;
        variantData.fragmentReflection.specEntries = specEntries;
        report->variants.push_back(std::move(variantData));
      }
    }
    std::cout << "[" << shaderReport.name << "] raw=" << shaderReport.rawCount
              << " compiled=" << shaderReport.compiledCount;
    if (shaderReport.errorCount > 0) {
      std::cout << " errors=" << shaderReport.errorCount;
    }
    std::cout << "\n";
    report->shaders.push_back(std::move(shaderReport));
  }
}

int RunBuildCommand(const BuildOptions& options) {
  BuildReport report;
  for (auto profile : options.backends) {
    report.diagnostics.registerProfile(ProfileTag(profile));
  }
  // Every family is a row of the declaration table; nothing else can register work.
  if (FamilyCount() == 0) {
    std::cerr << "No shaders registered in the declaration table.\n";
    return 1;
  }

  const bool compiling = !options.shaderDir.empty();

  StageCache stageCache;
  if (compiling && !options.noCache) {
    auto cacheDir = options.cacheDir.empty() ? options.outDir + "/stage_cache" : options.cacheDir;
    std::string cacheError;
    if (!stageCache.open(cacheDir, &cacheError)) {
      Diagnostic diagnostic;
      diagnostic.severity = Severity::Warning;
      diagnostic.layer = Layer::Compile;
      diagnostic.code = DiagnosticCode::CompileCacheDisabled;
      diagnostic.message = "stage cache disabled, compiling every stage: " + cacheError;
      report.diagnostics.report(std::move(diagnostic));
    }
  }
  StageCompiler stageCompiler(&stageCache);


  CompileAssembledFamilies(options, &stageCompiler, &report);

  if (!WriteReportJson(report, options.outDir)) {
    return 1;
  }

  // Must run before the variants are moved into the per-backend bundles below.
  if (!options.stageReportPath.empty() && !report.variants.empty()) {
    if (!WriteStageReport(options.stageReportPath, report.variants, &report.diagnostics)) {
      PrintDiagnostics(report.diagnostics);
      return 1;
    }
    std::cout << "Stage report written to: " << options.stageReportPath << "\n";
  }

  // Must run before the variants are moved into the per-backend bundles below, like the stage
  // report: the dump reads the same records.
  if (!options.stageDumpDir.empty() && !report.variants.empty()) {
    if (!WriteStageDump(options.stageDumpDir, report.variants, &report.diagnostics)) {
      PrintDiagnostics(report.diagnostics);
      return 1;
    }
    std::cout << "Stage dump written to: " << options.stageDumpDir << "\n";
  }

  // Cross-language contract checks, as two rule registries: the artifact rules read what the
  // build produced (the clip contract fields, the reflection contracts, the inline expansion
  // policy), the source rules read the shader text (the op codes, the slot capacities, the
  // source references). A rule prints each violation itself; the counts gate the build, so a
  // mismatch fails here instead of surfacing as a runtime upload missing its target.
  if (!options.inlineReportPath.empty() &&
      !WriteInlineExpansionReport(options.inlineReportPath, report.inlineExpansions,
                                  &report.diagnostics)) {
    PrintDiagnostics(report.diagnostics);
    return 1;
  }
  ArtifactRuleContext artifactContext;
  artifactContext.variants = &report.variants;
  artifactContext.inlineExpansions = &report.inlineExpansions;
  for (const auto& rule : MakeArtifactRules()) {
    // Every rule reports each violation as a diagnostic; the returned count is for the tests
    // (RuleCountingTest pins it to the records the rule added) and is not gated on here.
    rule->check(artifactContext, &report.diagnostics);
  }
  if (!options.shaderDir.empty()) {
    SourceRuleContext sourceContext;
    sourceContext.shaderDir = options.shaderDir;
    for (const auto& rule : MakeSourceRules()) {
      rule->check(sourceContext, &report.diagnostics);
    }
  }
  PrintDiagnostics(report.diagnostics);
  // One summary for every failure source: the ledger is the single gate (every failure path
  // reports a record — the pairing is pinned by tests — so no separate counters are kept).
  const auto errorCount = report.diagnostics.count(Severity::Error);
  const auto unverifiableCount = report.diagnostics.count(Severity::Unverifiable);
  const auto warningCount = report.diagnostics.count(Severity::Warning);
  if (report.diagnostics.hasErrors()) {
    std::cerr << "Build failed: " << errorCount << " error(s), " << unverifiableCount
              << " unverifiable, " << warningCount
              << " warning(s); the bundles were not written. See shader_build_report.json.\n";
    return 1;
  }

  // Write bundle files grouped by backend (profileTag)
  if (!report.variants.empty()) {
    std::map<std::string, std::vector<VariantData>> byBackend;
    for (auto& v : report.variants) {
      byBackend[v.profileTag].push_back(std::move(v));
    }
    for (const auto& pair : byBackend) {
      std::string filename = "shader_bundle." + pair.first + ".bin";
      std::string path = options.outDir + "/" + filename;
      if (!WriteBundle(path, pair.first, pair.second, options.compress, &report.diagnostics)) {
        PrintDiagnostics(report.diagnostics);
        return 1;
      }
      std::cout << "Bundle written: " << filename << " (" << pair.second.size() << " entries)\n";
    }
  }

  if (compiling) {
    // The cache only counts; rendering the summary is the cli's job (NoSublayerPrints).
    std::cout << "[stage-cache] "
              << (stageCache.persistent() ? "persistent at " + stageCache.directoryPath()
                                          : std::string("memory only"));
    if (stageCache.persistent()) {
      std::cout << " (tool " << stageCache.toolIdentity().substr(0, 12) << ")";
    }
    std::cout << "\n";
    for (const auto& entry : stageCache.allStats()) {
      const auto& s = entry.second;
      std::cout << "[stage-cache] " << entry.first << ": requests=" << s.requests
                << " memory-hits=" << s.memoryHits << " disk-hits=" << s.diskHits
                << " executed=" << s.executed << " stored=" << s.stored << " corrupt=" << s.corrupt
                << "\n";
    }
  }
  return 0;
}

}  // namespace tgfx

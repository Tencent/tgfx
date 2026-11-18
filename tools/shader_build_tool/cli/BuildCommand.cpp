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
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <utility>
#include <vector>
#include "cli/ReportWriter.h"
#include "compile/ReflectionExtractor.h"
#include "compile/ShaderCompiler.h"
#include "compile/SpecCodegen.h"
#include "compile/StageCache.h"
#include "compile/StageCompiler.h"
#include "core/CapacityDefines.h"
#include "core/DiagnosticCodes.h"
#include "core/Diagnostics.h"
#include "core/ShaderSources.h"
#include "emit/Assembler.h"
#include "emit/ChainOpsGLSL.h"
#include "emit/ComposedShapes.h"
#include "emit/ProfileEmitter.h"
#include "gpu/shaders/FamilyDomains.h"
#include "gpu/shaders/KernelContract.h"
#include "gpu/shaders/Layout.h"
#include "gpu/shaders/PrecompiledShader.h"
#include "gpu/shaders/contract/ChainISA.h"
#include "gpu/shaders/contract/FragmentLibrary.h"
#include "gpu/shaders/contract/ShaderBundleContract.h"
#include "gpu/shaders/contract/SpecContract.h"
#include "output/BundleWriter.h"
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

namespace {

std::string DigestOfFile(const std::string& path, uint64_t* size, std::string* error) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    *error = "cannot read " + path;
    return {};
  }
  Sha256 hash;
  std::vector<char> chunk(1 << 16);
  uint64_t total = 0;
  while (file) {
    file.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
    const auto got = static_cast<size_t>(file.gcount());
    hash.update(chunk.data(), got);
    total += got;
  }
  *size = total;
  return Sha256::Hex(hash.finish());
}

std::string FileNameOf(const std::string& path) {
  const auto slash = path.find_last_of('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string DirectoryOf(const std::string& path) {
  const auto slash = path.find_last_of('/');
  return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}

}  // namespace

bool WriteStampManifest(const std::string& stampPath, const std::string& toolIdentity,
                        const std::vector<StampFileEntry>& entries, std::string* error) {
  std::string text = "tool=" + toolIdentity + "\nschema=1\n";
  for (const auto& entry : entries) {
    uint64_t size = 0;
    std::string digestError;
    const auto digest = DigestOfFile(entry.path, &size, &digestError);
    if (digest.empty()) {
      *error = "stamp: " + digestError;
      return false;
    }
    text += entry.backend + " " + digest + " " + std::to_string(size) + " " +
            FileNameOf(entry.path) + "\n";
  }
  const auto temp = stampPath + ".tmp";
  {
    std::ofstream file(temp, std::ios::binary | std::ios::trunc);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    file.close();
    if (!file.good()) {
      *error = "stamp: cannot write " + temp;
      return false;
    }
  }
  std::error_code code;
  std::filesystem::rename(temp, stampPath, code);
  if (code) {
    std::filesystem::remove(temp, code);
    *error = "stamp: cannot move " + temp + " onto " + stampPath;
    return false;
  }
  return true;
}

bool VerifyStampManifest(const std::string& stampPath, const std::string& toolIdentity,
                         std::string* error) {
  std::ifstream file(stampPath, std::ios::binary);
  if (!file.is_open()) {
    *error = "verify: no stamp at " + stampPath;
    return false;
  }
  const std::string fix = "fix: delete " + stampPath + " and rebuild";
  std::string line;
  if (!std::getline(file, line) || line.rfind("tool=", 0) != 0) {
    *error = "verify: " + stampPath + " is not a stamp manifest; " + fix;
    return false;
  }
  const auto recordedTool = line.substr(5);
  if (recordedTool != toolIdentity) {
    *error = "verify: the stamp was written by tool " + recordedTool.substr(0, 12) +
             " but the running tool is " + toolIdentity.substr(0, 12) + "; " + fix;
    return false;
  }
  if (!std::getline(file, line) || line != "schema=1") {
    *error = "verify: unknown stamp schema; " + fix;
    return false;
  }
  const auto dir = DirectoryOf(stampPath);
  size_t checked = 0;
  while (std::getline(file, line)) {
    if (line.empty()) {
      continue;
    }
    std::istringstream row(line);
    std::string backend;
    std::string digest;
    std::string name;
    uint64_t size = 0;
    if (!(row >> backend >> digest >> size >> name)) {
      *error = "verify: malformed stamp row '" + line + "'; " + fix;
      return false;
    }
    uint64_t actualSize = 0;
    std::string digestError;
    const auto actual = DigestOfFile(dir + "/" + name, &actualSize, &digestError);
    if (actual.empty()) {
      *error = "verify: " + digestError + "; " + fix;
      return false;
    }
    if (actualSize != size) {
      *error = "verify: " + name + " is " + std::to_string(actualSize) +
               " bytes, the stamp recorded " + std::to_string(size) + "; " + fix;
      return false;
    }
    if (actual != digest) {
      *error = "verify: " + name + " does not match its recorded digest; " + fix;
      return false;
    }
    ++checked;
  }
  if (checked == 0) {
    *error = "verify: the stamp records no bundles; " + fix;
    return false;
  }
  return true;
}

std::string UsageText() {
  return "Usage: shader_build_tool [options]\n"
         "  --shader-dir <path>   Directory containing shader sources\n"
         "  --out-dir <path>      Output directory for build artifacts\n"
         "  --backends <list>     Comma-separated backend list "
         "(opengl,opengles,vulkan,metal,webgpu,d3d12)\n"
         "\n"
         "  --compress            Compress data pool with zstd in output bundles\n"
         "  --cache-dir <path>    Persistent stage cache (default: <out-dir>/stage_cache)\n"
         "  --no-cache            Compile every stage, reading and writing no persistent cache\n"
         "  --cache-max-mb <mb>   Stage cache size cap in MiB (default 2048); opening the cache\n"
         "                        evicts entries oldest-first down to 80% of it\n"
         "\n"
         "  --stamp <path>        Write a stamp manifest after the bundles: the tool's identity\n"
         "                        and every bundle's digest, for --verify to check later\n"
         "  --verify <path>       Check a stamp manifest and exit: every bundle must match its\n"
         "                        recorded digest and the tool that wrote the stamp\n"
         "\n"
         "  --metal-sdk <sdk>     Platform of the Metal bundle: macosx (default), iphoneos or\n"
         "                        iphonesimulator. A metallib only works on its own platform\n"
         "  --metal-min-os <ver>  Minimum OS version of the Metal bundle, e.g. 15.0\n"
         "  --d3d12-shader-format <form>\n"
         "                        What a d3d12 bundle stores: dxbc (default, bytecode compiled\n"
         "                        here) or hlsl (source the backend compiles at module creation)\n";
}

void PrintUsage() {
  std::cerr << UsageText();
}

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
    } else if (std::strcmp(argv[i], "--compress") == 0) {
      options->compress = true;
    } else if (std::strcmp(argv[i], "--cache-dir") == 0 && i + 1 < argc) {
      options->cacheDir = argv[++i];
    } else if (std::strcmp(argv[i], "--no-cache") == 0) {
      options->noCache = true;
    } else if (std::strcmp(argv[i], "--cache-max-mb") == 0 && i + 1 < argc) {
      options->cacheMaxBytes = static_cast<uint64_t>(std::strtoull(argv[++i], nullptr, 10)) << 20;
    } else if (std::strcmp(argv[i], "--stamp") == 0 && i + 1 < argc) {
      options->stampPath = argv[++i];
    } else if (std::strcmp(argv[i], "--verify") == 0 && i + 1 < argc) {
      options->verifyPath = argv[++i];

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
  // The fragment source pack: every fragment with a source file is read verbatim and stored in the
  // bundle's fragment pool under its fragment id (0x40000000 | op), so the runtime assembler joins
  // the compiled-in manifest with the text without ever reading files. A missing or unreadable
  // source fails the build — the pack and the manifest cannot drift apart silently.
  for (const auto& emitterEntry : emitters) {
    if (emitterEntry.first != ShaderProfile::OpenGL &&
        emitterEntry.first != ShaderProfile::OpenGLES) {
      continue;
    }
    const std::string profileTag = ProfileTag(emitterEntry.first);
    for (size_t index = 0; index < FragmentCount(); ++index) {
      const FragmentDef* fragment = FragmentAt(index);
      if (fragment == nullptr || fragment->sourceFile == nullptr) {
        continue;
      }
      std::ifstream file(options.shaderDir + "/" + fragment->sourceFile, std::ios::binary);
      if (!file.is_open()) {
        RecordBuildError(
            &report->diagnostics, Layer::Sources, DiagnosticCode::SourcesUnreadable,
            fragment->functionName, profileTag, -1, -1,
            std::string("the fragment source ") + fragment->sourceFile + " cannot be read");
        continue;
      }
      std::ostringstream buffer;
      buffer << file.rdbuf();
      // The entry packs the fragment with its includes resolved: a raw file may carry #include
      // lines (the resource evaluators, the xp blend table) that a driver cannot consume, and the
      // shared sub-files' include guards keep the expanded copies from colliding.
      std::string expandError;
      const auto expanded = ExpandStageIncludes(buffer.str(), options.shaderDir, &expandError);
      if (expanded.empty()) {
        RecordBuildError(&report->diagnostics, Layer::Sources, DiagnosticCode::SourcesUnreadable,
                         fragment->functionName, profileTag, -1, -1,
                         std::string("the fragment source ") + fragment->sourceFile +
                             " failed to expand: " + expandError);
        continue;
      }
      ComposedPayload payload;
      payload.name = fragment->functionName;
      payload.id = 0x40000000u | static_cast<uint32_t>(fragment->op);
      // LF-normalized like every pack entry: the source files' CRLF would leave a stray \r on
      // each preprocessor line. The XP-coupled regions (the blend table's coverage composites,
      // which only the xfer entries serve) drop out here: a fragment entry is unconditional.
      std::string normalized = expanded;
      normalized.erase(std::remove(normalized.begin(), normalized.end(), '\r'), normalized.end());
      while (true) {
        const auto at = normalized.find("#if HAS_XP >= 1");
        if (at == std::string::npos) {
          break;
        }
        const auto contentStart = normalized.find('\n', at) + 1;
        int depth = 1;
        size_t cursor = contentStart;
        while (cursor < normalized.size() && depth > 0) {
          const auto ifAt = normalized.find("#if", cursor);
          const auto endAt = normalized.find("#endif", cursor);
          if (endAt == std::string::npos) {
            break;
          }
          if (ifAt != std::string::npos && ifAt < endAt) {
            ++depth;
            cursor = ifAt + 3;
          } else {
            --depth;
            cursor = endAt + 6;
          }
        }
        const auto endLine = normalized.find('\n', cursor);
        normalized.erase(at, (endLine == std::string::npos ? normalized.size() : endLine + 1) - at);
      }
      payload.blob.assign(normalized.begin(), normalized.end());
      report->composedByTag[profileTag].push_back(std::move(payload));
    }
    // The pack's non-op entries: the generated loaders (the chain-ops pragma's own expansion), the
    // interface chunk and tail template the assembler concatenates, and the shared dependency
    // files the op fragments call into.
    auto addEntry = [&report, &profileTag](uint32_t id, const char* name, std::string text) {
      // The pack serves driver-facing text: the source files' CRLF endings would leave a stray
      // \r on every preprocessor line ("unexpected tokens following #if"), so the entries ship
      // LF-normalized like the dialect text does.
      text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
      if (text.empty()) {
        RecordBuildError(&report->diagnostics, Layer::Sources, DiagnosticCode::SourcesUnreadable,
                         name, profileTag, -1, -1,
                         std::string("the pack entry ") + name + " is empty");
        return;
      }
      ComposedPayload payload;
      payload.name = name;
      payload.id = id;
      payload.blob.assign(text.begin(), text.end());
      report->composedByTag[profileTag].push_back(std::move(payload));
    };
    auto readFile = [&options](const char* path) {
      std::ifstream file(std::string(options.shaderDir) + "/" + path, std::ios::binary);
      if (!file.is_open()) {
        return std::string();
      }
      std::ostringstream buffer;
      buffer << file.rdbuf();
      return buffer.str();
    };
    // A dependency entry packs its includes resolved, for the same reason the fragment sources do.
    auto readExpanded = [&options](const char* path) {
      const std::string raw = [&] {
        std::ifstream file(std::string(options.shaderDir) + "/" + path, std::ios::binary);
        if (!file.is_open()) {
          return std::string();
        }
        std::ostringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
      }();
      if (raw.empty()) {
        return raw;
      }
      std::string expandError;
      return ExpandStageIncludes(raw, options.shaderDir, &expandError);
    };
    addEntry(FragmentPackTail, "ChainTail", readFile("entry/ChainTail.glsl"));
    addEntry(FragmentPackDepAnalyticClipMath, "AnalyticClipMath",
             readExpanded("coverage/analytic_clip_math.inc"));
    addEntry(FragmentPackDepTextureSwizzle, "TextureSwizzle",
             readExpanded("source/texture_read_swizzle.inc"));
    addEntry(FragmentPackDepTextureSampling, "TextureSampling",
             readExpanded("source/texture_sampling.inc"));
    auto readXp = [&options](int hasXp) {
      const std::string raw = "#define HAS_XP " + std::to_string(hasXp) +
                              "\n#define TGFX_DECLARED_CHAIN 1\n#include "
                              "\"pipeline/chain/pointwise_xp_support.inc\"\n";
      std::string expandError;
      auto text = ExpandStageIncludes(raw, options.shaderDir, &expandError);
      if (text.empty()) {
        return text;
      }
      // The driver rejects expression conditionals, so each HAS_XP test folds to a constant for
      // this form: the >= form holds for both, the == forms pick one.
      auto fold = [&text](const std::string& token, bool keep) {
        size_t offset = 0;
        while ((offset = text.find(token, offset)) != std::string::npos) {
          text.replace(offset, token.size(), keep ? "#if 1" : "#if 0");
          offset += 5;
        }
      };
      fold("#if HAS_XP >= 1", true);
      fold("#if HAS_XP == 1", hasXp == 1);
      fold("#if HAS_XP == 2", hasXp == 2);
      // Strip the folded directives themselves: a kept region loses its "#if 1" line, a dropped
      // region (with its #else, if any) disappears whole, so the packed entry carries no
      // conditional at all — the form was chosen when the entry was built.
      auto stripFolded = [&text] {
        std::string out;
        size_t cursor = 0;
        while (cursor < text.size()) {
          const auto lineEnd = text.find('\n', cursor);
          const std::string line = text.substr(
              cursor, lineEnd == std::string::npos ? std::string::npos : lineEnd - cursor + 1);
          cursor = lineEnd == std::string::npos ? text.size() : lineEnd + 1;
          if (line == "#if 1\n") {
            continue;  // keep the region, drop the directive
          }
          if (line == "#if 0\n") {
            int depth = 1;
            while (cursor < text.size() && depth > 0) {
              const auto end2 = text.find('\n', cursor);
              const std::string inner = text.substr(
                  cursor, end2 == std::string::npos ? std::string::npos : end2 - cursor + 1);
              cursor = end2 == std::string::npos ? text.size() : end2 + 1;
              if (inner.rfind("#if", 0) == 0) {
                ++depth;
              } else if (inner.rfind("#endif", 0) == 0) {
                --depth;
              }
            }
            continue;
          }
          out += line;
        }
        text = std::move(out);
      };
      stripFolded();
      return text;
    };
    addEntry(FragmentPackXpDstTexture, "XpDstTexture", readXp(1));
    addEntry(FragmentPackXpFramebufferFetch, "XpFramebufferFetch", readXp(2));
    // The packed pieces' `#if` branches, resolved textually into per-fact forms: the driver
    // rejects `#if` directives in the assembled text, and the forms differ structurally.
    auto resolveBranch = [](const std::string& text, const std::string& directive, bool keep) {
      const auto begin = text.find(directive);
      if (begin == std::string::npos) {
        return text;
      }
      const auto contentStart = text.find('\n', begin) + 1;
      int depth = 1;
      size_t cursor = contentStart;
      size_t endifAt = std::string::npos;
      while (cursor < text.size()) {
        const auto ifAt = text.find("#if", cursor);
        const auto endAt = text.find("#endif", cursor);
        if (endAt == std::string::npos) {
          return text;
        }
        if (ifAt != std::string::npos && ifAt < endAt) {
          ++depth;
          cursor = ifAt + 3;
        } else {
          --depth;
          if (depth == 0) {
            endifAt = endAt;
            break;
          }
          cursor = endAt + 6;
        }
      }
      if (endifAt == std::string::npos) {
        return text;
      }
      const auto endLine = text.find('\n', endifAt) + 1;
      if (keep) {
        return text.substr(0, begin) + text.substr(contentStart, endifAt - contentStart) +
               text.substr(endLine);
      }
      return text.substr(0, begin) + text.substr(endLine);
    };
    const std::string gradientRaw = readExpanded("pipeline/chain/pointwise_gradient.inc");
    addEntry(FragmentPackGradientLut, "GradientLut",
             resolveBranch(gradientRaw, "#if NTEX > 0", true));
    addEntry(FragmentPackGradientSimple, "GradientSimple",
             resolveBranch(gradientRaw, "#if NTEX > 0", false));
    std::string leafFetchRaw = readExpanded("entry/ChainLeafFetch.glsl");
    // The tiled leaves' coordinate-varying picker references every leaf varying, which the plan
    // program only declares for the leaves it samples; the tiled path stays on the interpreter, so
    // the picker leaves the pack.
    {
      const auto pickerAt = leafFetchRaw.find("vec3 chainLeafCoordVarying(int leafIndex) {");
      if (pickerAt != std::string::npos) {
        size_t depth = 0;
        size_t end = std::string::npos;
        for (size_t i = leafFetchRaw.find('{', pickerAt); i < leafFetchRaw.size(); ++i) {
          if (leafFetchRaw[i] == '{') {
            ++depth;
          } else if (leafFetchRaw[i] == '}') {
            if (--depth == 0) {
              end = i + 1;
              break;
            }
          }
        }
        if (end != std::string::npos) {
          leafFetchRaw.erase(pickerAt, end - pickerAt);
        }
      }
    }
    addEntry(FragmentPackLeafFetchStrict, "LeafFetchStrict",
             resolveBranch(leafFetchRaw, "#if HAS_SUBSET", true));
    addEntry(FragmentPackLeafFetchPlain, "LeafFetchPlain",
             resolveBranch(leafFetchRaw, "#if HAS_SUBSET", false));
  }
  for (size_t familyIndex = 0; familyIndex < FamilyCount(); ++familyIndex) {
    const Family& family = Families()[familyIndex];
    const auto info = AssembledFamilyInfo(family);

    ShaderReport shaderReport;
    shaderReport.name = family.name;
    shaderReport.rawCount = 0;
    shaderReport.compiledCount = 0;
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
    // The gate names whose stubbed bodies already compiled once for this family: a gate's
    // signature is the same in every variant that carries its file, so one stub compile per
    // distinct gate proves the closed form's call sites, and a later variant only re-checks the
    // gates it adds.
    std::set<std::string> gatesStubValidated;
    for (const auto& variant : variants) {
      const auto assembled = AssembleVariant(family, variant);
      // The assembled text carries module #includes; the compiler never sees one, so the same
      // expansion the handwritten files get runs here first.
      std::string expandError;
      const auto vertText =
          ExpandStageIncludes(assembled.vertText, options.shaderDir, &expandError);
      const auto fragText =
          ExpandStageIncludes(assembled.fragText, options.shaderDir, &expandError);
      // The interpreter assembly carries the chain-ops pragma the kernels use; the
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
          compiler->extractReflection(info.name, variant.ordinal, variant.ordinal, vertResult.spirv,
                                      fragResult.spirv, &report->diagnostics);
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
          RecordBuildError(
              &report->diagnostics, Layer::Emit, DiagnosticCode::SpecLayoutDrift, info.name, "",
              static_cast<int>(variant.ordinal), static_cast<int>(variant.ordinal),
              "the declared spec ids and the compiled constant_id set disagree: " + detail);
          continue;
        }
      }
      if (!LayoutMatchesReflection(layout, reflection)) {
        RecordBuildError(&report->diagnostics, Layer::Emit, DiagnosticCode::EmitFailed, info.name,
                         "", static_cast<int>(variant.ordinal), static_cast<int>(variant.ordinal),
                         "the assembled text's reflection does not match the derived layout");
        continue;
      }
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
      // The gate declarations ride in the assembled fragment's inc files, so each variant scans
      // its own expanded text: a variant that prunes a gated file carries neither the pragma nor
      // the function, and a gate whose condition reads a field this variant's spec table does not
      // carry stays in every form (dropped here, never wrapped). The closed gates' stub bodies
      // are compiled once per distinct gate: a stub the call sites cannot accept fails the build
      // here, not on a device.
      std::vector<SpecGateInfo> supportedGates = {};
      if (!specEntries.empty()) {
        for (auto gate : ScanGatePragmas(fragExpanded, info.name, &report->diagnostics)) {
          if (!GateEvaluatable(gate, specEntries)) {
            continue;
          }
          if (!NormalizeGateCondition(&gate, specEntries)) {
            RecordBuildError(
                &report->diagnostics, Layer::Compile, DiagnosticCode::SpecGateUnmatched, info.name,
                "", static_cast<int>(variant.ordinal), static_cast<int>(variant.ordinal),
                "the gate '" + gate.name +
                    "' condition is not a single-field bit test; the runtime evaluates "
                    "`(SPEC_<field>() & <mask>) != 0` only");
            continue;
          }
          supportedGates.push_back(gate);
        }
        std::vector<SpecGateInfo> unvalidatedGates = {};
        for (const auto& gate : supportedGates) {
          if (gatesStubValidated.find(gate.name) == gatesStubValidated.end()) {
            unvalidatedGates.push_back(gate);
          }
        }
        if (!unvalidatedGates.empty()) {
          const auto stubbed = StubOutGatedFunctions(fragExpanded, unvalidatedGates);
          if (stubbed.empty()) {
            RecordBuildError(&report->diagnostics, Layer::Compile,
                             DiagnosticCode::SpecGateUnmatched, info.name, "",
                             static_cast<int>(variant.ordinal), static_cast<int>(variant.ordinal),
                             "a gate pragma does not precede a stubbable function (scalar or "
                             "vector return type required)");
            continue;
          }
          const auto& stubResult =
              compiler->compileSpirv(PrependDefines(stubbed, fragDefines),
                                     ShaderStageType::Fragment, info.name, variant.ordinal, false);
          if (!stubResult.success) {
            RecordBuildError(&report->diagnostics, Layer::Compile,
                             DiagnosticCode::SpecGateUnmatched, info.name, "",
                             static_cast<int>(variant.ordinal), static_cast<int>(variant.ordinal),
                             "the closed gates' stub form failed to compile: " + stubResult.error);
            continue;
          }
          for (const auto& gate : unvalidatedGates) {
            gatesStubValidated.insert(gate.name);
          }
        }
      }
      shaderReport.compiledCount++;
      for (auto& emitterEntry : emitters) {
        if (!PermutationCompilesForBackend(static_cast<uint16_t>(familyIndex), variant.ordinal,
                                           emitterEntry.first)) {
          continue;
        }
        EmitInput input;
        input.info = &info;
        input.familyIndex = static_cast<uint16_t>(familyIndex);
        input.vertIndex = variant.ordinal;
        input.fragIndex = variant.ordinal;
        input.vertSource = &vertExpanded;
        input.fragSource = &fragExpanded;
        input.vertDefines = &vertDefines;
        input.fragDefines = &fragDefines;
        input.vertSpirv = &vertResult.spirv;
        input.fragStage = &fragResult;
        input.fragGates = &supportedGates;
        input.fragSpecEntries = &specEntries;
        input.diagnostics = &report->diagnostics;
        auto stages = emitterEntry.second->emit(input, compiler);
        if (!stages) {
          continue;
        }
        // The pre-composed payloads: for a GL-text chain variant, every manifest shape this
        // variant hosts is spelled from the exact bytes the bundle stores, so the runtime's
        // lookup serves a text byte-identical to what its own splice would have produced. The
        // canonical proxy compile (defines + splice over the unconverted source) fails the build
        // when the emitter's marshalling drifts, before any payload is stored.
        if (std::string(family.name) == "Chain" &&
            stages->payloadKind == BundleFormat::Payload::Text &&
            (emitterEntry.first == ShaderProfile::OpenGL ||
             emitterEntry.first == ShaderProfile::OpenGLES) &&
            !stages->fragmentBlob.empty()) {
          const uint32_t chainVariantId =
              VariantIdOf(static_cast<uint16_t>(familyIndex), variant.ordinal);
          const std::string profileTag = ProfileTag(emitterEntry.first);
          for (const auto& shape : ComposedShapeManifest()) {
            const uint8_t xferSlot = static_cast<uint8_t>(2 + family.moduleCount);
            if (shape.profile != DimValueOf(family, variant, 2, 0) ||
                shape.geometry != variant.geometry ||
                DimValueOf(family, variant, xferSlot, 0) < shape.minHasXp) {
              continue;
            }
            const auto idValues = ComposedSpecIdValues(family, variant, shape.composition);
            // The payload must be the dialect text the bundle stores, not the canonical source:
            // the canonical form still declares the spec constants as `layout(constant_id)`, and a
            // prepended #define would rewrite that declaration into invalid code, while the stored
            // form carries the idempotent macro blocks the runtime's own injection also expects.
            std::string text(stages->fragmentBlob.begin(), stages->fragmentBlob.end());
            if (!PrependChainSpecDefinesGLSL(&text, idValues) ||
                !ComposeChainFragmentGLSL(shape.composition, &text)) {
              RecordBuildError(&report->diagnostics, Layer::Output, DiagnosticCode::OutputWrite,
                               info.name, profileTag, static_cast<int>(variant.ordinal),
                               static_cast<int>(variant.ordinal),
                               std::string("the composed shape ") + shape.name + " did not splice");
              continue;
            }
            ComposedPayload payload;
            payload.name = shape.name;
            payload.id = ComposedFragmentId(chainVariantId, shape.composition);
            payload.blob.assign(text.begin(), text.end());
            report->composedByTag[profileTag].push_back(std::move(payload));
          }
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
              << " compiled=" << shaderReport.compiledCount << "\n";
    report->shaders.push_back(std::move(shaderReport));
  }
}

int RunBuildCommand(const BuildOptions& options) {
  BuildReport report;
  for (auto profile : options.backends) {
    report.diagnostics.registerProfile(ProfileTag(profile));
  }
  // Every family is a row of the declaration table; nothing else can register work.
  if (!options.verifyPath.empty()) {
    std::string identityError;
    const auto identity = ComputeToolIdentity(&identityError);
    if (identity.empty()) {
      std::cerr << "verify: " << identityError << "\n";
      return 1;
    }
    std::string error;
    if (!VerifyStampManifest(options.verifyPath, identity, &error)) {
      std::cerr << error << "\n";
      return 1;
    }
    std::cout << "Bundles verified.\n";
    return 0;
  }

  if (FamilyCount() == 0) {
    std::cerr << "No shaders registered in the declaration table.\n";
    return 1;
  }

  const bool compiling = !options.shaderDir.empty();

  StageCache stageCache;
  if (compiling && !options.noCache) {
    auto cacheDir = options.cacheDir.empty() ? options.outDir + "/stage_cache" : options.cacheDir;
    std::string cacheError;
    if (!stageCache.open(cacheDir, options.cacheMaxBytes, &cacheError)) {
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

  // Cross-language contract checks, as two rule registries: the artifact rules read what the
  // build produced (the reflection contracts, the binding layout), the source rules read the
  // shader text (the macro names, the pointwise dispatch, the spec readers). A rule prints each
  // violation itself; the counts gate the build, so a mismatch fails here instead of surfacing as
  // a runtime upload missing its target.
  ArtifactRuleContext artifactContext;
  artifactContext.variants = &report.variants;
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
  std::vector<StampFileEntry> stampEntries = {};
  if (!report.variants.empty()) {
    std::map<std::string, std::vector<VariantData>> byBackend;
    for (auto& v : report.variants) {
      byBackend[v.profileTag].push_back(std::move(v));
    }
    for (const auto& pair : byBackend) {
      std::string filename = "shader_bundle." + pair.first + ".bin";
      std::string path = options.outDir + "/" + filename;
      const auto composedIt = report.composedByTag.find(pair.first);
      const std::vector<ComposedPayload> noComposed = {};
      const auto& composed =
          composedIt == report.composedByTag.end() ? noComposed : composedIt->second;
      if (!WriteBundle(path, pair.first, pair.second, composed, options.compress,
                       &report.diagnostics)) {
        PrintDiagnostics(report.diagnostics);
        return 1;
      }
      std::cout << "Bundle written: " << filename << " (" << pair.second.size() << " entries, "
                << composed.size() << " composed)\n";
      stampEntries.push_back({pair.first, path});
    }
  }
  // The stamp is a manifest of what this tool just wrote, so a later --verify can prove the
  // bundles on disk are these ones.
  if (!options.stampPath.empty()) {
    std::string identityError;
    const auto identity = ComputeToolIdentity(&identityError);
    std::string stampError;
    if (identity.empty() ||
        !WriteStampManifest(options.stampPath, identity, stampEntries, &stampError)) {
      std::cerr << (identity.empty() ? identityError : stampError) << "\n";
      return 1;
    }
  }

  if (compiling) {
    // The cache only counts; rendering the summary is the cli's job (NoSublayerPrints).
    std::cout << "[stage-cache] "
              << (stageCache.persistent() ? "persistent at " + stageCache.directoryPath()
                                          : std::string("memory only"));
    if (stageCache.persistent()) {
      std::cout << " (tool " << stageCache.toolIdentity().substr(0, 12) << ")";
      if (stageCache.bytesPrunedAtOpen() > 0) {
        std::cout << ", pruned " << (stageCache.bytesPrunedAtOpen() >> 20) << "MB";
      }
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

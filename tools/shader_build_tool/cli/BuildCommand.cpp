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
#include "compile/SPIRVLibrary.h"
#include "compile/ShaderCompiler.h"
#include "compile/SpecCodegen.h"
#include "compile/StageCache.h"
#include "compile/StageCompiler.h"
#include "core/DiagnosticCodes.h"
#include "core/Diagnostics.h"
#include "core/ShaderSources.h"
#include "emit/ChainOpsGLSL.h"
#include "gpu/ir/OperatorManifest.h"
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
         "  --library-bundle <path>\n"
         "                        Scan the function sources and write the single shader library\n"
         "                        bundle (no variant bundles are built)\n"
         "  --library-languages <list>\n"
         "                        Comma-separated sections the library bundle carries (glsl,\n"
         "                        glsl,msl); the Metal section translates every function to MSL\n"
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
    if (std::strcmp(argv[i], "--library-bundle") == 0 && i + 1 < argc) {
      options->libraryBundlePath = argv[++i];
    } else if (std::strcmp(argv[i], "--library-languages") == 0 && i + 1 < argc) {
      options->libraryLanguages = argv[++i];
    } else if (std::strcmp(argv[i], "--shader-dir") == 0 && i + 1 < argc) {
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

/// The only interface check: the reflection of the assembled text must equal the derived layout
/// (uniforms per block, samplers, vertex inputs), or the declaration and the assembly disagree.

}  // namespace

void CompileAssembledFamilies(const BuildOptions& options, StageCompiler* /*compiler*/,
                              BuildReport* report) {
  if (options.shaderDir.empty()) {
    return;
  }
  // The leaf fetch's shared text without the two interpreter-only helpers (the named block and
  // the varying picker); the text pack and the Vulkan op wrapper consume the same stripped copy.
  std::string strippedLeafFetch = {};
  auto stripFetchHelpers = [](std::string text) {
    for (const char* signature : {"vec3 chainLeafCoordVarying(int leafIndex) {",
                                  "highp vec2 chainLeafCoord(vec3 coord, int leafIndex) {"}) {
      const auto functionAt = text.find(signature);
      if (functionAt == std::string::npos) {
        continue;
      }
      size_t depth = 0;
      size_t end = std::string::npos;
      for (size_t i = text.find('{', functionAt); i < text.size(); ++i) {
        if (text[i] == '{') {
          ++depth;
        } else if (text[i] == '}') {
          if (--depth == 0) {
            end = i + 1;
            break;
          }
        }
      }
      if (end != std::string::npos) {
        text.erase(functionAt, end - functionAt);
      }
    }
    return text;
  };
  // A dependency entry packs its includes resolved, for the same reason the fragment sources do.
  // Strips comments and blank lines from a shader source before it enters the pack: the text ships
  // to the bundle and gets compiled, so a comment is pure payload. `#pragma` lines survive (the
  // spec-injection pass rewrites them), and string literals do not exist in GLSL.
  auto stripComments = [](const std::string& text) {
    std::string out;
    out.reserve(text.size());
    bool inBlock = false;
    for (size_t i = 0; i < text.size(); ++i) {
      const char c = text[i];
      const char next = i + 1 < text.size() ? text[i + 1] : '\0';
      if (inBlock) {
        if (c == '*' && next == '/') {
          inBlock = false;
          ++i;
        }
        continue;
      }
      if (c == '/' && next == '/') {
        while (i + 1 < text.size() && text[i] != '\n') {
          ++i;
        }
        continue;
      }
      if (c == '/' && next == '*') {
        inBlock = true;
        ++i;
        continue;
      }
      out.push_back(c);
    }
    std::string compact;
    compact.reserve(out.size());
    size_t lineStart = 0;
    while (lineStart <= out.size()) {
      size_t lineEnd = out.find('\n', lineStart);
      if (lineEnd == std::string::npos) {
        lineEnd = out.size();
      }
      const std::string line = out.substr(lineStart, lineEnd - lineStart);
      if (line.find_first_not_of(" \t\r") != std::string::npos) {
        compact += line;
        compact.push_back('\n');
      }
      if (lineEnd == out.size()) {
        break;
      }
      lineStart = lineEnd + 1;
    }
    return compact;
  };
  auto readExpanded = [&options, &stripComments](const char* path) {
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
    return stripComments(ExpandStageIncludes(raw, options.shaderDir, &expandError));
  };
  const std::vector<ShaderProfile>& emitters = options.backends;
  // The fragment source pack: every fragment with a source file is read verbatim and stored in the
  // bundle's fragment pool under its fragment id (0x40000000 | op), so the runtime assembler joins
  // the compiled-in manifest with the text without ever reading files. A missing or unreadable
  // source fails the build — the pack and the manifest cannot drift apart silently.
  for (auto emitterProfile : emitters) {
    if (emitterProfile != ShaderProfile::OpenGL && emitterProfile != ShaderProfile::OpenGLES) {
      continue;
    }
    const std::string profileTag = ProfileTag(emitterProfile);
    [[maybe_unused]] auto resolveBranch = [](const std::string& text, const std::string& directive,
                                             bool keep) {
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
      const auto expanded =
          stripComments(ExpandStageIncludes(buffer.str(), options.shaderDir, &expandError));
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
    addEntry(FragmentPackDepAnalyticClipMath, "AnalyticClipMath",
             readExpanded("coverage/analytic_clip_math.inc"));
    addEntry(FragmentPackDepTextureSwizzle, "TextureSwizzle",
             readExpanded("source/texture_read_swizzle.inc"));
    addEntry(FragmentPackDepTextureSampling, "TextureSampling",
             readExpanded("source/texture_sampling.inc"));
    auto readXp = [&options, &stripComments](int hasXp) {
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
      // The assembled entry fetches its leaves itself (the tiled forms are outside its slice), so
      // the tiled machinery the support file pulls in under NTEX > 0 is dead in this form.
      fold("#if NTEX > 0", false);
      // Resolve the folded directives: a kept region loses its "#if 1" line and a dropped one
      // (with its #else, if any) disappears whole, together with every directive line that owned
      // them — the packed entry must carry no unbalanced conditional at all, because the form was
      // chosen when the entry was built and the driver would reject the leftovers. The remaining
      // conditionals (the include guards) are copied verbatim.
      auto stripFolded = [&text] {
        struct Frame {
          bool keep = true;
          /// Set for the frames the folding created: their directives are dropped, the guard
          /// frames pass theirs through.
          bool folded = false;
        };
        std::vector<Frame> frames;
        std::string out;
        auto kept = [&frames] {
          for (const auto& frame : frames) {
            if (!frame.keep) {
              return false;
            }
          }
          return true;
        };
        size_t cursor = 0;
        while (cursor < text.size()) {
          const auto lineEnd = text.find('\n', cursor);
          const std::string line = text.substr(
              cursor, lineEnd == std::string::npos ? std::string::npos : lineEnd - cursor + 1);
          cursor = lineEnd == std::string::npos ? text.size() : lineEnd + 1;
          if (line == "#if 1\n" || line == "#if 0\n") {
            frames.push_back({line == "#if 1\n", true});
            continue;
          }
          if (line.rfind("#if", 0) == 0) {
            frames.push_back({true, false});
            if (kept()) {
              out += line;
            }
            continue;
          }
          if (line.rfind("#else", 0) == 0 || line.rfind("#elif", 0) == 0) {
            if (frames.empty()) {
              out += line;
            } else if (frames.back().folded) {
              frames.back().keep = false;
            } else if (kept()) {
              out += line;
            }
            continue;
          }
          if (line.rfind("#endif", 0) == 0) {
            const bool folded = !frames.empty() && frames.back().folded;
            if (!frames.empty()) {
              frames.pop_back();
            }
            if (!folded && kept()) {
              out += line;
            }
            continue;
          }
          if (kept()) {
            out += line;
          }
        }
        text = std::move(out);
      };
      stripFolded();
      return stripComments(text);
    };
    addEntry(FragmentPackXpDstTexture, "XpDstTexture", readXp(1));
    addEntry(FragmentPackXpFramebufferFetch, "XpFramebufferFetch", readXp(2));
    std::string leafFetchRaw = stripFetchHelpers(readExpanded("entry/ChainLeafFetch.glsl"));
    strippedLeafFetch = leafFetchRaw;
    addEntry(FragmentPackLeafFetchPlain, "LeafFetchPlain", leafFetchRaw);
  }
  // The Vulkan linking route's op library: one patched SPIR-V module per pure operator, compiled
  // from the same sources the text pack ships (a dummy entry forces glslang to emit the function;
  // the surgery then strips the entry and exports the operator for linking).
  for (auto emitterProfile : emitters) {
    if (emitterProfile != ShaderProfile::Vulkan && emitterProfile != ShaderProfile::Metal) {
      // The composed payloads are SPIR-V: Vulkan links them, Metal links and translates them to
      // MSL at draw time, so both profiles carry the same pool.
      continue;
    }
    const std::string profileTag = ProfileTag(emitterProfile);
    // One linkable SPIR-V module per new IR operator. The manifest owns the id, symbol, return
    // type, parameters and include recipe; nothing here reads the old chain table.
    for (size_t operatorIndex = 0; operatorIndex < OperatorCount(); ++operatorIndex) {
      const auto* desc = OperatorAt(operatorIndex);
      if (desc == nullptr || desc->glslSymbol == nullptr || desc->includes.empty()) {
        continue;
      }
      bool unsupported = false;
      for (const auto& param : desc->params) {
        if (param.isArray) {
          unsupported = true;
          break;
        }
      }
      if (unsupported) {
        continue;
      }
      std::string text = "#version 450\n";
      if (desc->prelude != nullptr) {
        text += desc->prelude;
      }
      bool needsDummySampler = false;
      bool needsRectSampler = false;
      for (const auto& param : desc->params) {
        if (param.type == SLType::Texture2DSampler) {
          needsDummySampler = true;
        } else if (param.type == SLType::Texture2DRectSampler) {
          needsRectSampler = true;
        }
      }
      if (needsDummySampler) {
        text += "layout(set = 2, binding = 15) uniform sampler2D dummyLeafTex;\n";
      }
      if (needsRectSampler) {
        text += "layout(set = 2, binding = 14) uniform sampler2DRect dummyRectTex;\n";
      }
      if (desc->leafSamplers) {
        for (int sampler = 0; sampler < 4; ++sampler) {
          text += "layout(set = 2, binding = " + std::to_string(sampler) +
                  ") uniform sampler2D TextureSampler_" + std::to_string(sampler) + ";\n";
        }
      }
      for (const auto* include : desc->includes) {
        text += readExpanded(include);
      }
      text += "\nlayout(location = 0) out vec4 dummyOut;\nvoid main() {\n  dummyOut = ";
      std::string widenPrefix;
      std::string widenSuffix;
      switch (desc->returnType) {
        case SLType::Float:
          widenPrefix = "vec4(";
          widenSuffix = ")";
          break;
        case SLType::Float2:
          widenPrefix = "vec4(";
          widenSuffix = ", 0.0, 0.0)";
          break;
        case SLType::Float3:
          widenPrefix = "vec4(";
          widenSuffix = ", 0.0)";
          break;
        default:
          break;
      }
      text += widenPrefix;
      text += desc->glslSymbol;
      text += "(";
      for (size_t argument = 0; argument < desc->params.size(); ++argument) {
        if (argument > 0) {
          text += ", ";
        }
        const auto& param = desc->params[argument];
        if (param.isArray) {
          text += "vec4[" + std::to_string(param.arrayCount) + "](";
          for (uint32_t element = 0; element < param.arrayCount; ++element) {
            if (element > 0) {
              text += ", ";
            }
            text += "vec4(1.0)";
          }
          text += ")";
          continue;
        }
        switch (param.type) {
          case SLType::Float:
            text += "0.0";
            break;
          case SLType::Int:
            text += "0";
            break;
          case SLType::Float2:
            text += "vec2(1.0)";
            break;
          case SLType::Float3:
            text += "vec3(1.0)";
            break;
          case SLType::Float4:
            text += "vec4(1.0)";
            break;
          case SLType::Float3x3:
            text += "mat3(1.0)";
            break;
          case SLType::Float4x4:
            text += "mat4(1.0)";
            break;
          case SLType::Int4:
            text += "ivec4(0)";
            break;
          case SLType::Texture2DSampler:
            text += "dummyLeafTex";
            break;
          case SLType::Texture2DRectSampler:
            text += "dummyRectTex";
            break;
          default:
            break;
        }
      }
      text += ")";
      text += widenSuffix;
      text += ";\n}\n";
      std::string buildError;
      auto patched =
          CompileSPIRVLibraryModule(text, desc->glslSymbol, desc->glslSymbol, &buildError);
      if (patched.empty()) {
        RecordBuildError(&report->diagnostics, Layer::Output, DiagnosticCode::OutputWrite,
                         desc->glslSymbol, profileTag, -1, -1,
                         std::string("the SPIR-V operator module did not build: ") + buildError);
        continue;
      }
      std::string reflectError;
      const auto reflected = ReflectFunctionSignature(patched, desc->glslSymbol, &reflectError);
      if (reflected.size() != desc->params.size()) {
        RecordBuildError(&report->diagnostics, Layer::Output, DiagnosticCode::OutputWrite,
                         desc->glslSymbol, profileTag, -1, -1,
                         "the operator declares " + std::to_string(desc->params.size()) +
                             " parameters but the module compiled " +
                             std::to_string(reflected.size()) +
                             (reflectError.empty() ? std::string() : ": " + reflectError));
        continue;
      }
      bool signatureMatches = true;
      for (size_t argument = 0; argument < desc->params.size(); ++argument) {
        const auto& param = desc->params[argument];
        OpTable::Param expected = OpTable::Param::Vec4;
        if (param.isArray) {
          expected = OpTable::Param::Vec4Array8;
        } else {
          switch (param.type) {
            case SLType::Float:
              expected = OpTable::Param::Float;
              break;
            case SLType::Int:
              expected = OpTable::Param::Int;
              break;
            case SLType::Float2:
              expected = OpTable::Param::Vec2;
              break;
            case SLType::Float3:
              expected = OpTable::Param::Vec3;
              break;
            case SLType::Float4:
              expected = OpTable::Param::Vec4;
              break;
            case SLType::Float3x3:
              expected = OpTable::Param::Mat3;
              break;
            case SLType::Float4x4:
              expected = OpTable::Param::Mat4;
              break;
            case SLType::Int4:
              expected = OpTable::Param::IVec4;
              break;
            case SLType::Texture2DSampler:
            case SLType::Texture2DRectSampler:
              expected = OpTable::Param::SampledImage;
              break;
            default:
              break;
          }
        }
        if (reflected[argument] != expected) {
          signatureMatches = false;
          RecordBuildError(&report->diagnostics, Layer::Output, DiagnosticCode::OutputWrite,
                           desc->glslSymbol, profileTag, -1, -1,
                           "parameter " + std::to_string(argument) + " of " + desc->glslSymbol +
                               " drifted from the manifest");
        }
      }
      if (!signatureMatches) {
        continue;
      }
      ComposedPayload payload;
      payload.name = desc->glslSymbol;
      payload.id = FragmentPackSPIRVOpBase | static_cast<uint32_t>(desc->id);
      const auto* bytes = reinterpret_cast<const uint8_t*>(patched.data());
      payload.blob.assign(bytes, bytes + patched.size() * sizeof(uint32_t));
      report->composedByTag[profileTag].push_back(std::move(payload));
    }
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

  // Write bundle files grouped by backend (profileTag). Every requested backend gets a bundle:
  // the runtime assembles its programs from the composed pools and text packs, so a bundle with
  // no per-variant entries is still a complete artifact.
  std::vector<StampFileEntry> stampEntries = {};
  {
    std::map<std::string, std::vector<VariantData>> byBackend;
    for (auto& v : report.variants) {
      byBackend[v.profileTag].push_back(std::move(v));
    }
    for (auto profile : options.backends) {
      byBackend[ProfileTag(profile)];
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

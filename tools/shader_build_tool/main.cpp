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
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <regex>
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
#include "gpu/shaders/PrecompiledShader.h"
#include "gpu/shaders/AllShaderFamilies.h"

namespace tgfx {

struct BuildOptions {
  std::string shaderDir;
  std::string outDir;
  std::vector<std::string> backends;
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

// GL and GLES store their own spelling of the template instead of text regenerated from SPIR-V:
// the regenerated form's call-site temporaries (param/param_1) and normalized block names
// measured +79% lines on the chain interpreter. Both entries below first run the standard GLSL
// preprocessor (PreprocessGLSL, the one the SPIR-V backends compile through), so every #if block
// is resolved for the variant and the stored text carries only live code. EmitDirectGLSL330Impl
// then rewrites the preprocessed 450 text into the target dialect: lower the version header,
// drop the `set = N` / `binding = N` layout members (the runtime binds the UBO at a fixed point
// and the samplers in declaration order), drop varying locations, and fold location ids to
// literals. Reflection keeps coming from the common SPIR-V (same template, same names, same
// std140 layout), so the loader contract and the ABI are unchanged. Returns an empty string when
// the source does not carry the expected version, so the caller fails the variant closed.
static std::string EmitDirectGLSL330Impl(const std::string& source, ShaderStageType stage,
                                         const std::string& versionLine, bool esDialect,
                                         bool fbfVariant);

// Desktop GL stores the text after the standard GLSL preprocessor (the same one the SPIR-V
// backends compile through): every #if block is resolved for this variant, so the stored blob
// carries only the live code, and the driver neither re-parses dead branches nor depends on the
// emitter's own reading of the conditionals. Returns an empty string on failure.
static std::string EmitDirectGLSL330(const std::string& source, ShaderStageType stage) {
  std::string error;
  auto preprocessed = PreprocessGLSL(source, stage, &error);
  if (preprocessed.empty()) {
    std::cerr << "  direct-GL 330 preprocessing failed: " << error << "\n";
    return {};
  }
  return EmitDirectGLSL330Impl(preprocessed, stage, "#version 330", false, false);
}

// Evaluates an integer constant expression of a layout id after preprocessing: decimal literals,
// unary +/-, binary + - *, and parentheses. Returns false on any other form (an identifier
// included), so the caller keeps the original text instead of storing a wrong literal.
static bool FoldIntegerExpression(const std::string& expr, int64_t* value) {
  size_t pos = 0;
  auto skip = [&]() {
    while (pos < expr.size() && isspace(static_cast<unsigned char>(expr[pos]))) {
      ++pos;
    }
  };
  std::function<bool(int64_t*)> parseSum;
  std::function<bool(int64_t*)> parseUnary = [&](int64_t* out) -> bool {
    skip();
    if (pos >= expr.size()) {
      return false;
    }
    auto c = expr[pos];
    if (c == '+' || c == '-') {
      ++pos;
      int64_t inner = 0;
      if (!parseUnary(&inner)) {
        return false;
      }
      *out = c == '-' ? -inner : inner;
      return true;
    }
    if (c == '(') {
      ++pos;
      if (!parseSum(out)) {
        return false;
      }
      skip();
      if (pos >= expr.size() || expr[pos] != ')') {
        return false;
      }
      ++pos;
      return true;
    }
    auto start = pos;
    if (isdigit(static_cast<unsigned char>(c))) {
      while (pos < expr.size() && isdigit(static_cast<unsigned char>(expr[pos]))) {
        ++pos;
      }
      *out = std::stoll(expr.substr(start, pos - start));
      return true;
    }
    return false;
  };
  auto parseProduct = [&](int64_t* out) -> bool {
    if (!parseUnary(out)) {
      return false;
    }
    while (true) {
      skip();
      if (pos >= expr.size() || expr[pos] != '*') {
        return true;
      }
      ++pos;
      int64_t rhs = 0;
      if (!parseUnary(&rhs)) {
        return false;
      }
      *out *= rhs;
    }
  };
  parseSum = [&](int64_t* out) -> bool {
    if (!parseProduct(out)) {
      return false;
    }
    while (true) {
      skip();
      if (pos >= expr.size() || (expr[pos] != '+' && expr[pos] != '-')) {
        return true;
      }
      auto op = expr[pos++];
      int64_t rhs = 0;
      if (!parseProduct(&rhs)) {
        return false;
      }
      *out = op == '+' ? *out + rhs : *out - rhs;
    }
  };
  int64_t result = 0;
  if (!parseSum(&result)) {
    return false;
  }
  skip();
  if (pos != expr.size()) {
    return false;
  }
  *value = result;
  return true;
}

// ES-300 direct emission (the GLES sibling of EmitDirectGLSL330): the same preprocessing and
// rewrites, plus four ES-specific ones:
//   1. `#version 300 es` + the two default precision lines;
//   2. every float-family declaration decorated `highp` (ES defaults to mediump and many devices
//      implement it as fp16, so the decoration is a numerical contract, not cosmetics);
//   3. fbf variants (HAS_XP == 2): subpassInput declaration and subpassLoad() replaced with the
//      GL_EXT_shader_framebuffer_fetch dialect — the inout fragment output is the dst read;
//   4. `out` fragment output becomes `inout` on fbf variants only.
// (EmitDirectGLSLES300 is defined after the core below.)
static std::string EmitDirectGLSL330Impl(const std::string& source, ShaderStageType stage,
                                         const std::string& versionLine, bool esDialect,
                                         bool fbfVariant) {
  const std::string versionToken = "#version 450";
  auto versionPos = source.find(versionToken);
  if (versionPos == std::string::npos) {
    return {};
  }
  auto lineStart = source.rfind('\n', versionPos);
  bool atFileHead =
      versionPos == 0 || (lineStart != std::string::npos &&
                          source.compare(lineStart + 1, versionToken.size(), versionToken) == 0);
  if (!atFileHead) {
    return {};
  }
  std::string result = source;
  result.replace(versionPos, versionToken.size(), versionLine);
  if (esDialect) {
    // Default precision right after the version line, mirroring the regenerated ES-300 form
    // (int is highp-capable everywhere; float must be stated — ES has no implicit default).
    auto versionEnd = result.find('\n', versionPos);
    if (versionEnd != std::string::npos) {
      // Insert after the #version line itself — the templates open with a license comment
      // block, so the file's first newline sits BEFORE the version token; inserting there
      // would put a precision statement ahead of #version, which ES rejects.
      result.insert(versionEnd + 1, "precision mediump float;\nprecision highp int;\n");
    }
    if (fbfVariant) {
      // The framebuffer-fetch dialect: the extension line goes right after the precision
      // defaults, ahead of every declaration that references it.
      auto insertPos = result.find("precision highp int;\n", versionPos);
      if (insertPos != std::string::npos) {
        result.insert(insertPos + sizeof("precision highp int;\n") - 1,
                      "#extension GL_EXT_shader_framebuffer_fetch : require\n");
      }
    }
  }

  std::string out;
  out.reserve(result.size());

  size_t cursor = 0;
  while (cursor < result.size()) {
    auto lineEnd = result.find('\n', cursor);
    auto line =
        result.substr(cursor, lineEnd == std::string::npos ? std::string::npos : lineEnd - cursor);
    auto directive = line.find_first_not_of(" \t");
    if (directive != std::string::npos && line[directive] == '#') {
      // Only #version, #extension and #pragma survive preprocessing; they pass through.
      auto directiveComment = line.find("//");
      out += directiveComment == std::string::npos ? line : line.substr(0, directiveComment);
      out += '\n';
      cursor = lineEnd == std::string::npos ? result.size() : lineEnd + 1;
      continue;
    }
    // Drop lines left empty by the preprocessor.
    auto comment = line.find("//");
    auto code = comment == std::string::npos ? line : line.substr(0, comment);
    if (code.find_first_not_of(" \t\r") == std::string::npos) {
      cursor = lineEnd == std::string::npos ? result.size() : lineEnd + 1;
      continue;
    }
    // Rewrite layout(...) lists: drop set/binding, drop varying locations, fold location ids.
    {
      auto open = code.find("layout(");
      while (open != std::string::npos) {
        // The matching parenthesis: a layout id may be a parenthesized expression.
        auto close = std::string::npos;
        int depth = 0;
        for (auto i = open + 6; i < code.size(); ++i) {
          if (code[i] == '(') {
            ++depth;
          } else if (code[i] == ')' && --depth == 0) {
            close = i;
            break;
          }
        }
        if (close == std::string::npos) {
          break;
        }
        std::string inner = code.substr(open + 7, close - open - 7);
        std::string kept;
        size_t tokenCursor = 0;
        while (tokenCursor <= inner.size()) {
          auto comma = inner.find(',', tokenCursor);
          auto token = inner.substr(
              tokenCursor, comma == std::string::npos ? std::string::npos : comma - tokenCursor);
          auto headWs = token.find_first_not_of(" \t");
          if (headWs == std::string::npos) {
            if (comma == std::string::npos) break;
            tokenCursor = comma + 1;
            continue;
          }
          std::string member = token.substr(headWs);
          if (member.compare(0, 4, "set ") != 0 && member.compare(0, 8, "binding ") != 0) {
            // GLSL 330 allows layout(location) only on vertex inputs and fragment outputs;
            // varyings (vertex out / fragment in) must link by name — 4.1 introduced explicit
            // varying locations. spirv-cross already strips them in its 330 output; the direct
            // emission drops the member instead. Attribute/out declarations are identified by
            // the `in`/`out` keyword on the declaration line.
            auto isVarying =
                code.find(" in ") != std::string::npos
                    ? stage == ShaderStageType::Fragment
                    : (code.find(" out ") != std::string::npos && stage == ShaderStageType::Vertex);
            auto eq = member.find("= ");
            if (member.rfind("location", 0) == 0 && eq != std::string::npos) {
              if (isVarying) {
                member.clear();  // drop the location member from a varying declaration
              } else {
                // Fold `location = <expr>` to a literal: GLSL 330 / ES 300 require one, and the
                // preprocessor leaves macro-built ids as arithmetic such as `2 + 1`.
                auto expr = member.substr(eq + 2);
                int64_t folded = 0;
                if (FoldIntegerExpression(expr, &folded)) {
                  member = "location = " + std::to_string(folded);
                }
              }
            }
            if (!member.empty()) {
              if (!kept.empty()) {
                kept += ", ";
              }
              kept += member;
            }
          }
          if (comma == std::string::npos) break;
          tokenCursor = comma + 1;
        }
        std::string replacement = kept.empty() ? "" : "layout(" + kept + ")";
        code.replace(open, close - open + 1, replacement);
        open = code.find("layout(", open + replacement.size());
      }
    }
    out += code;
    out += '\n';
    cursor = lineEnd == std::string::npos ? result.size() : lineEnd + 1;
  }
  if (esDialect) {
    // Blanket `highp` decoration on every float-family declaration. The regenerated ES-300
    // output decorates uniformly (45k occurrences, zero exceptions — the fragment default is
    // mediump, and many devices implement it as fp16, so the decoration is a numerical
    // contract). Decoration sites: declarations (globals/locals/UBO members/params), not
    // usages; the pattern is a type keyword at a declaration position not already decorated
    // and not a struct field separator. int-family stays as written (default highp).
    static const std::regex floatType(R"((^|[^A-Za-z0-9_])(float|vec[234]|mat[234])\s+[A-Za-z_])");
    std::string decorated;
    decorated.reserve(out.size() + 4096);
    size_t cursor2 = 0;
    while (cursor2 < out.size()) {
      auto lineEnd2 = out.find('\n', cursor2);
      auto line2 = out.substr(
          cursor2, lineEnd2 == std::string::npos ? std::string::npos : lineEnd2 - cursor2);
      // Preprocessor lines, precision statements, and the extension line pass through.
      auto nonSpace = line2.find_first_not_of(" \t");
      bool passthrough = nonSpace == std::string::npos || line2[nonSpace] == '#' ||
                         line2.compare(nonSpace, 9, "precision") == 0;
      if (!passthrough) {
        std::string rebuilt;
        size_t pos = 0;
        while (pos < line2.size()) {
          std::smatch match;
          std::string rest = line2.substr(pos);
          if (std::regex_search(rest, match, floatType) && !match.empty()) {
            auto at = pos + static_cast<size_t>(match.position(2));
            // Already decorated (the char run before the type is `highp `/`mediump `) or a
            // usage inside an expression: only decorate when the preceding token chain ends
            // with a declarator context. Heuristic sufficient for these sources: the match is
            // a declaration when the line's first token is a type/qualifier or we are inside
            // a parameter list — verified by the calibration diff against the regenerated
            // text, which fails the build on any mismatch.
            rebuilt += line2.substr(pos, at - pos);
            auto prefix = line2.substr(0, at);
            auto endsWith = [](const std::string& s, const char* suffix) {
              auto len = strlen(suffix);
              return s.size() >= len && s.compare(s.size() - len, len, suffix) == 0;
            };
            bool decoratedAlready = endsWith(prefix, "highp ") || endsWith(prefix, "mediump ") ||
                                    endsWith(prefix, "lowp ");
            if (!decoratedAlready) {
              rebuilt += "highp ";
            }
            // Skip exactly the matched type keyword (its length is known; do not re-scan the
            // text — 'float' contains characters outside any type charset and would stall the
            // cursor).
            auto typeLength = static_cast<size_t>(match.length(2));
            rebuilt += line2.substr(at, typeLength);
            pos = at + typeLength;
          } else {
            rebuilt += line2.substr(pos);
            pos = line2.size();
          }
        }
        line2 = rebuilt;
      }
      decorated += line2;
      if (lineEnd2 != std::string::npos) {
        decorated += '\n';
      }
      cursor2 = lineEnd2 == std::string::npos ? out.size() : lineEnd2 + 1;
    }
    out = decorated;
    if (fbfVariant) {
      // Framebuffer-fetch dialect on the fragment stage: subpass input declaration removed,
      // subpassLoad() reads the inout output, and the output declaration becomes inout. The
      // templates confine these to xp_porter_duff_fbf.inc plus the single output declaration,
      // so targeted replacements cover the whole surface.
      auto declPos = out.find("layout(input_attachment_index = 0");
      if (declPos != std::string::npos) {
        auto declEnd = out.find('\n', declPos);
        out.erase(declPos,
                  declEnd == std::string::npos ? out.size() - declPos : declEnd - declPos + 1);
      }
      auto outPos = out.find("layout(location = 0) out ");
      if (outPos != std::string::npos) {
        out.replace(outPos, 24, "layout(location = 0) inout ");
      }
      auto loadPos = out.find("subpassLoad(");
      while (loadPos != std::string::npos) {
        auto argOpen = out.find('(', loadPos);
        auto argClose = out.find(')', argOpen);
        auto arg = out.substr(argOpen + 1, argClose - argOpen - 1);
        // Strip the sampler name to its base (tgfx_SubpassInput -> the inout variable).
        out.replace(loadPos, argClose - loadPos + 1, arg);
        loadPos = out.find("subpassLoad(", loadPos + arg.size());
      }
      // `vec4 dstColor = tgfx_SubpassInput;` — the erased declaration leaves the uniform
      // sampler name as a plain identifier; rename it to the fragment output.
      auto dstPos = out.find("= tgfx_SubpassInput;");
      while (dstPos != std::string::npos) {
        out.replace(dstPos, 20, "= fragColor;");
        dstPos = out.find("= tgfx_SubpassInput;", dstPos + 13);
      }
    }
  }
  return out;
}

std::string EmitDirectGLSLES300(const std::string& source, ShaderStageType stage, bool fbfVariant) {
  std::string error;
  auto preprocessed = PreprocessGLSL(source, stage, &error);
  if (preprocessed.empty()) {
    std::cerr << "  direct-ES 300 preprocessing failed: " << error << "\n";
    return {};
  }
  return EmitDirectGLSL330Impl(preprocessed, stage, "#version 300 es", true, fbfVariant);
}

// Reuse of compiled stages. Every expensive compilation (GLSL to SPIR-V, the optimized Vulkan
// SPIR-V, MSL to metallib, GLSL to WGSL) is looked up by a key built from everything that feeds it:
// the exact text the compiler receives, the stage, the compiler options, and the identity of this
// tool (see StageCacheKey). The memory layer removes the repeats within one run, such as a vertex
// permutation shared by many fragment variants; the persistent layer (StageCache) lets a later run
// skip every stage whose inputs did not change. Failures are only remembered for this run.
struct SpirvCacheEntry {
  bool success = false;
  std::vector<uint32_t> spirv;
  std::string error;
};
static std::map<std::string, SpirvCacheEntry> spirvCache;

static const SpirvCacheEntry& CompileGLSLShared(const std::string& source, ShaderStageType stage,
                                                const std::string& shaderName,
                                                uint32_t variantIndex, bool optimize) {
  const char* kind = optimize ? "spirv-optimized" : "spirv";
  auto& cache = StageCache::Get();
  auto& stats = cache.stats(kind);
  stats.requests++;
  // shaderc receives shaderName_variant_N as the input file name, so it is part of the input.
  auto key = StageCacheKey(kind)
                 .add(shaderName + "_variant_" + std::to_string(variantIndex))
                 .add(static_cast<uint64_t>(stage))
                 .add(static_cast<uint64_t>(optimize))
                 .add(source)
                 .finish();
  auto it = spirvCache.find(key);
  if (it != spirvCache.end()) {
    stats.memoryHits++;
    return it->second;
  }
  SpirvCacheEntry entry;
  std::vector<uint8_t> payload;
  if (cache.load(key, &payload, &stats) && payload.size() % 4 == 0) {
    entry.success = true;
    entry.spirv.resize(payload.size() / 4);
    std::memcpy(entry.spirv.data(), payload.data(), payload.size());
  } else {
    stats.executed++;
    auto result = CompileGLSL(source, stage, shaderName, variantIndex, optimize);
    entry.success = result.success;
    entry.spirv = std::move(result.spirv);
    entry.error = std::move(result.error);
    if (entry.success) {
      auto* bytes = reinterpret_cast<const uint8_t*>(entry.spirv.data());
      cache.store(key, std::vector<uint8_t>(bytes, bytes + entry.spirv.size() * 4), &stats);
    }
  }
  return spirvCache.emplace(std::move(key), std::move(entry)).first->second;
}

struct BlobCacheEntry {
  bool success = false;
  std::vector<uint8_t> bytes;
  std::string error;
};
static std::map<std::string, BlobCacheEntry> blobCache;

// Looks a backend artifact up in both cache layers, or runs compile() and caches what it returns.
// compile fills bytes and returns true, or returns false with an error.
template <typename Compile>
static const BlobCacheEntry& CompileBlobShared(const std::string& kind, std::string key,
                                               Compile compile) {
  auto& cache = StageCache::Get();
  auto& stats = cache.stats(kind);
  stats.requests++;
  auto it = blobCache.find(key);
  if (it != blobCache.end()) {
    stats.memoryHits++;
    return it->second;
  }
  BlobCacheEntry entry;
  if (cache.load(key, &entry.bytes, &stats)) {
    entry.success = true;
  } else {
    stats.executed++;
    entry.success = compile(&entry.bytes, &entry.error);
    if (entry.success) {
      cache.store(key, entry.bytes, &stats);
    }
  }
  return blobCache.emplace(std::move(key), std::move(entry)).first->second;
}

// The Metal compiler's version and the macOS SDK it targets, queried once. Part of every metallib
// key: a new Xcode can compile the same MSL to different bytes.
static const std::string& MetalToolchainKey() {
  static const std::string key = MetalCompilerFingerprint() + "\n" + MetalToolchainVersion();
  return key;
}

static std::vector<uint8_t> CompileMetallibShared(const std::string& msl, ShaderStageType stage,
                                                  std::string* error) {
  auto key = StageCacheKey("metallib")
                 .add(MetalToolchainKey())
                 .add(static_cast<uint64_t>(stage))
                 .add(msl)
                 .finish();
  const auto& entry = CompileBlobShared(
      "metallib", std::move(key), [&](std::vector<uint8_t>* bytes, std::string* compileError) {
        *bytes = CompileMSLToMetallib(msl, stage, compileError);
        return !bytes->empty();
      });
  if (!entry.success) {
    *error = entry.error;
    return {};
  }
  return entry.bytes;
}

static CompileResult CompileWGSLShared(const std::string& source, ShaderStageType stage,
                                       const std::string& shaderName, uint32_t variantIndex) {
  auto key = StageCacheKey("wgsl")
                 .add(shaderName + "_variant_" + std::to_string(variantIndex))
                 .add(static_cast<uint64_t>(stage))
                 .add(source)
                 .finish();
  const auto& entry = CompileBlobShared(
      "wgsl", std::move(key), [&](std::vector<uint8_t>* bytes, std::string* compileError) {
        auto result = CompileGLSLToWGSL(source, stage, shaderName, variantIndex);
        if (!result.success) {
          *compileError = result.error;
          return false;
        }
        bytes->assign(result.wgsl.begin(), result.wgsl.end());
        return true;
      });
  CompileResult result;
  result.success = entry.success;
  result.error = entry.error;
  result.wgsl.assign(entry.bytes.begin(), entry.bytes.end());
  return result;
}

static std::map<std::tuple<std::string, uint32_t, uint32_t>, ReflectionResult> reflectionCache;

static const ReflectionResult& ExtractReflectionShared(const std::string& shaderName,
                                                       uint32_t vertIndex, uint32_t fragIndex,
                                                       const std::vector<uint32_t>& vertSpirv,
                                                       const std::vector<uint32_t>& fragSpirv) {
  auto key = std::make_tuple(shaderName, vertIndex, fragIndex);
  auto it = reflectionCache.find(key);
  if (it == reflectionCache.end()) {
    it = reflectionCache.emplace(std::move(key), ExtractReflection(vertSpirv, fragSpirv)).first;
  }
  return it->second;
}

// source is the shader's text from the build's single read of the sources (nullptr in report-only
// mode); the bundle's source digest is computed from the same text.
static ShaderReport CompileOneShader(const PrecompiledShaderInfo& info, const BuildOptions& options,
                                     const ShaderSource* source,
                                     std::vector<VariantData>* outVariants,
                                     std::vector<InlineExpansionRecord>* outInlineExpansions,
                                     std::map<std::string, uint64_t>* profileErrorCounts) {
  ShaderReport report;
  std::set<uint32_t> analyzedFrags;
  report.name = info.name;
  auto vertDomain = info.vertDomain;
  auto fragDomain = info.fragDomain;
  report.rawCount = vertDomain.totalCount() * fragDomain.totalCount();
  report.compiledCount = 0;
  report.errorCount = 0;

  // Unreadable files and broken includes were rejected when the sources were loaded, so a
  // present source is complete.
  std::string vertSource = source != nullptr ? source->vertex : "";
  std::string fragSource = source != nullptr ? source->fragment : "";

  // The compile list comes from the matcher rules' reachable sets (the Compose single source of
  // truth in the family headers), enumerated at build time. This replaces the former cartesian
  // domain walk filtered by ShouldCompile; the --audit mode verified both sides agree for every
  // shader before the switch, so the bundle content is unchanged.
  auto reachable = EnumerateReachablePermutations(info.name);
  if (!reachable) {
    std::cerr << "  ERROR: no rule enumerator for " << info.name
              << ": every shader must be migrated to the Compose pattern\n";
    report.errorCount++;
    RecordCommonArtifactError(profileErrorCounts);
    return report;
  }
  for (const auto& permutation : *reachable) {
    uint32_t vi = permutation.first;
    uint32_t fi = permutation.second;

    if (options.reportOnly || vertSource.empty() || fragSource.empty()) {
      continue;
    }

    auto vertDefines = vertDomain.defineListFor(vi);
    auto fragDefines = fragDomain.defineListFor(fi);

    // Compile vertex shader. The shared cache also covers the multiple-frag-per-vert reuse the
    // old per-backend vertCache handled, plus the cross-backend repeats.
    const auto& vertResult = CompileGLSLShared(PrependDefines(vertSource, vertDefines),
                                               ShaderStageType::Vertex, info.name, vi, false);
    if (!vertResult.success) {
      std::cerr << "  " << vertResult.error << "\n";
      report.errorCount++;
      RecordCommonArtifactError(profileErrorCounts);
      continue;
    }
    const auto* vertSpirv = &vertResult.spirv;

    const auto& fragResult = CompileGLSLShared(PrependDefines(fragSource, fragDefines),
                                               ShaderStageType::Fragment, info.name, fi, false);
    if (!fragResult.success) {
      std::cerr << "  " << fragResult.error << "\n";
      report.errorCount++;
      RecordCommonArtifactError(profileErrorCounts);
      continue;
    }

    // The unoptimized SPIR-V still has one function per GLSL function and one OpFunctionCall per
    // call site, so it shows what every backend's driver ends up inlining.
    if (analyzedFrags.insert(fi).second) {
      outInlineExpansions->push_back({info.name, fi, AnalyzeInlineExpansion(fragResult.spirv)});
    }

    // Extract reflection from SPIR-V (shared across backend passes, like the compiles above).
    const auto& reflection =
        ExtractReflectionShared(info.name, vi, fi, *vertSpirv, fragResult.spirv);
    // Counted only here — after both stages compiled successfully — so compiledCount is the
    // true "compiled" figure, distinct from rawCount (the reachable-permutation total) and
    // independent of backend-level exclusions counted below.
    report.compiledCount++;

    for (const auto& backend : options.backends) {
      // Backend-specific exclusions (the FBF variants) share one source of truth with the
      // closure verifier; see PermutationCompilesForBackend.
      if (!PermutationCompilesForBackend(info, vi, fi, backend)) {
        continue;
      }
      std::vector<uint8_t> vertBlob;
      std::vector<uint8_t> fragBlob;

      if (backend == "vulkan") {
        // Re-compile with optimization for smaller SPIR-V output.
        auto expandedVertOpt = PrependDefines(vertSource, vertDefines);
        const auto& vertOpt =
            CompileGLSLShared(expandedVertOpt, ShaderStageType::Vertex, info.name, vi, true);
        auto expandedFragOpt = PrependDefines(fragSource, fragDefines);
        const auto& fragOpt =
            CompileGLSLShared(expandedFragOpt, ShaderStageType::Fragment, info.name, fi, true);
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
        // The unoptimized SPIR-V is intentional here: xcrun produces byte-identical metallibs
        // whether fed the optimized or unoptimized translation (verified 2026-08-19), so the
        // extra optimized compile would only slow the build down.
        auto mslVert = TranslateToMSL(*vertSpirv, ShaderStageType::Vertex);
        auto mslFrag = TranslateToMSL(fragResult.spirv, ShaderStageType::Fragment);
        if (!mslVert.success || !mslFrag.success) {
          std::cerr << "  MSL translation error: "
                    << (mslVert.success ? mslFrag.error : mslVert.error) << "\n";
          report.errorCount++;
          RecordBackendArtifactError(backend, profileErrorCounts);
          continue;
        }
        std::string metalError;
        vertBlob = CompileMetallibShared(mslVert.msl, ShaderStageType::Vertex, &metalError);
        if (!vertBlob.empty()) {
          fragBlob = CompileMetallibShared(mslFrag.msl, ShaderStageType::Fragment, &metalError);
        }
        if (vertBlob.empty() || fragBlob.empty()) {
          std::cerr << "  metallib compilation failed for " << info.name << " [vert=" << vi
                    << " frag=" << fi << "]: " << metalError << "\n";
          report.errorCount++;
          RecordBackendArtifactError(backend, profileErrorCounts);
          continue;
        }
      } else if (backend == "webgpu") {
        // WebGPU needs its own GLSL form (separated texture/sampler bindings), so re-expand and
        // recompile like the vulkan branch instead of reusing the combined-sampler SPIR-V.
        auto expandedVertWgsl = PrependDefines(vertSource, vertDefines);
        auto expandedFragWgsl = PrependDefines(fragSource, fragDefines);
        auto wgslVert = CompileWGSLShared(expandedVertWgsl, ShaderStageType::Vertex, info.name, vi);
        auto wgslFrag =
            CompileWGSLShared(expandedFragWgsl, ShaderStageType::Fragment, info.name, fi);
        if (!wgslVert.success || !wgslFrag.success) {
          std::cerr << "  WGSL translation error: "
                    << (wgslVert.success ? wgslFrag.error : wgslVert.error) << "\n";
          report.errorCount++;
          RecordBackendArtifactError(backend, profileErrorCounts);
          continue;
        }
        vertBlob.assign(wgslVert.wgsl.begin(), wgslVert.wgsl.end());
        fragBlob.assign(wgslFrag.wgsl.begin(), wgslFrag.wgsl.end());
      } else if (backend == "opengl" || backend == "opengles") {
        if (backend == "opengl") {
          // Desktop GL direct emission (see EmitDirectGLSL330): store the preprocessed template
          // in its own 330 spelling. The mandatory compile check below runs the exact stored
          // text through glslang's OpenGL target, so a template using syntax beyond GL 330 (or
          // a leak of Vulkan-only qualifiers like subpassInput into a desktop variant) fails
          // the build here instead of surfacing as a runtime module-creation fallback.
          auto directVert =
              EmitDirectGLSL330(PrependDefines(vertSource, vertDefines), ShaderStageType::Vertex);
          auto directFrag =
              EmitDirectGLSL330(PrependDefines(fragSource, fragDefines), ShaderStageType::Fragment);
          if (directVert.empty() || directFrag.empty()) {
            std::cerr << "  direct-GL 330 emission failed (preprocessing or missing #version 450) "
                      << "for " << info.name << " [vert=" << vi << " frag=" << fi << "]\n";
            report.errorCount++;
            RecordBackendArtifactError(backend, profileErrorCounts);
            continue;
          }
          // Direct-emission verification is layered, because glslang's 330 front-end is stricter
          // than the real drivers (it rejects the very no-binding-block / located-out forms the
          // regenerated 330 output has shipped in for months, and macro-folded layout ids):
          //   1. Semantics: the common 450 SPIR-V compile above already validates every
          //      expression of the same preprocessed source.
          //   2. Form: the assertions below fail closed on any transform leak (a surviving
          //      set/binding qualifier, a Vulkan-only subpassInput reaching a desktop variant).
          //   3. Driver acceptance: the byte-parity consistency suite compiles the stored text
          //      on the actual GL driver.
          // A word-boundary hazard: variable names like `offset = ` contain the substring
          // "set = ", so the leak check must see the layout-qualifier context (the token is
          // preceded by '(' or ','), the same layout-qualifier context test.
          auto hasLayoutMember = [](const std::string& text, const std::string& member) -> int64_t {
            std::string token = member + " ";
            size_t cursor = 0;
            while ((cursor = text.find(token, cursor)) != std::string::npos) {
              size_t before = cursor;
              while (before > 0 && (text[before - 1] == ' ' || text[before - 1] == '\t')) {
                --before;
              }
              if (before > 0 && (text[before - 1] == '(' || text[before - 1] == ',')) {
                return static_cast<int64_t>(cursor);
              }
              ++cursor;
            }
            return -1;
          };
          auto assertDirectForm = [&](const std::string& text, const char* stage) {
            // Runs on the preprocessed text, so everything left is live for this variant: a
            // surviving set/binding qualifier (the strip missed a form), a missing version
            // rewrite, a surviving conditional directive, or Vulkan-only subpass syntax reaching
            // a desktop variant all fail the variant.
            static const std::regex conditional(
                R"((^|\n)[ \t]*#[ \t]*(if|ifdef|ifndef|elif|else|endif|define)\b)");
            auto setPos = hasLayoutMember(text, "set =");
            auto bindingPos = hasLayoutMember(text, "binding =");
            bool leak = text.find("#version 330") == std::string::npos ||
                        text.find("subpassInput") != std::string::npos ||
                        text.find("subpassLoad") != std::string::npos ||
                        std::regex_search(text, conditional);
            if (setPos >= 0 || bindingPos >= 0 || leak) {
              std::cerr << "  direct-GL 330 form assertion failed (" << stage << ") for "
                        << info.name << " [vert=" << vi << " frag=" << fi << "]";
              for (auto [pos, label] :
                   {std::pair<int64_t, const char*>{setPos, "set"}, {bindingPos, "binding"}}) {
                if (pos >= 0) {
                  std::cerr << " [" << label << " @ "
                            << text.substr(std::max<size_t>(0, static_cast<size_t>(pos) - 40), 90)
                            << "]";
                }
              }
              std::cerr << "\n";
              report.errorCount++;
              RecordBackendArtifactError(backend, profileErrorCounts);
              return false;
            }
            return true;
          };
          if (!assertDirectForm(directVert, "vert") || !assertDirectForm(directFrag, "frag")) {
            continue;
          }
          vertBlob.assign(directVert.begin(), directVert.end());
          fragBlob.assign(directFrag.begin(), directFrag.end());
        } else {
          // Direct ES-300 storage (see EmitDirectGLSLES300). The text is preprocessed, so every
          // check below sees only live code: the ES header, no surviving set/binding member
          // (preceded by '(' or ',', as `offset = ` contains "set = "), no conditional
          // directive, and no Vulkan subpass syntax left after the framebuffer-fetch rewrite.
          bool fbfVariant = info.fragDomain.valueOf(fi, XP_DEFINE_NAME) == XP_FRAMEBUFFER_FETCH;
          auto dv = EmitDirectGLSLES300(PrependDefines(vertSource, vertDefines),
                                        ShaderStageType::Vertex, false);
          auto df = EmitDirectGLSLES300(PrependDefines(fragSource, fragDefines),
                                        ShaderStageType::Fragment, fbfVariant);
          if (dv.empty() || df.empty()) {
            std::cerr << "  direct-ES emission failed (preprocessing or missing #version 450) "
                      << "for " << info.name << " [vert=" << vi << " frag=" << fi << "]\n";
            report.errorCount++;
            RecordBackendArtifactError(backend, profileErrorCounts);
            continue;
          }
          auto esFormLeak = [](const std::string& text) -> const char* {
            static const std::regex conditional(
                R"((^|\n)[ \t]*#[ \t]*(if|ifdef|ifndef|elif|else|endif|define)\b)");
            if (text.find("#version 300 es") == std::string::npos ||
                text.find("precision mediump float;") == std::string::npos) {
              return "ES header";
            }
            for (const char* member : {"set = ", "binding = "}) {
              size_t at = 0;
              while ((at = text.find(member, at)) != std::string::npos) {
                size_t before = at;
                while (before > 0 && (text[before - 1] == ' ' || text[before - 1] == '\t')) {
                  --before;
                }
                if (before > 0 && (text[before - 1] == '(' || text[before - 1] == ',')) {
                  return "surviving set/binding";
                }
                ++at;
              }
            }
            if (std::regex_search(text, conditional)) {
              return "surviving conditional";
            }
            if (text.find("subpassInput") != std::string::npos ||
                text.find("subpassLoad") != std::string::npos) {
              return "subpass syntax";
            }
            return nullptr;
          };
          const char* leak = esFormLeak(dv);
          if (leak == nullptr) {
            leak = esFormLeak(df);
          }
          if (leak != nullptr) {
            std::cerr << "  direct-ES form assertion failed (" << leak << ") for " << info.name
                      << " [vert=" << vi << " frag=" << fi << "]\n";
            report.errorCount++;
            RecordBackendArtifactError(backend, profileErrorCounts);
            continue;
          }
          vertBlob.assign(dv.begin(), dv.end());
          fragBlob.assign(df.begin(), df.end());
        }
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
// interface, so any violation fails the audit.
static int RunAuditMode() {
  const auto& factories = ShaderRegistry::All();
  size_t audited = 0;
  size_t violations = 0;
  for (const auto& factory : factories) {
    auto shader = factory();
    auto info = shader->info();
    auto reachable = EnumerateReachablePermutations(info.name);
    if (!reachable) {
      std::cerr << "[audit] " << info.name << ": ERROR no rule enumerator\n";
      violations++;
      continue;
    }
    audited++;
    size_t invalid = 0;
    for (const auto& pair : *reachable) {
      if (!IsBuildablePermutation(info, pair.first, pair.second)) {
        invalid++;
        violations++;
        std::cout << "[audit] " << info.name << ": INVALID vert=" << pair.first
                  << " frag=" << pair.second << " (out of range or mirrored dimensions disagree)\n";
      }
    }
    if (invalid == 0) {
      std::cout << "[audit] " << info.name << ": OK (" << reachable->size()
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
  for (const auto& backend : options.backends) {
    report.profileErrorCounts.emplace(backend, 0);
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

  if (compiling && !options.noCache) {
    auto cacheDir = options.cacheDir.empty() ? options.outDir + "/stage_cache" : options.cacheDir;
    std::string cacheError;
    if (!tgfx::StageCache::Get().open(cacheDir, &cacheError)) {
      std::cerr << "  WARNING: stage cache disabled, compiling every stage: " << cacheError << "\n";
    }
  }

  uint32_t totalErrors = 0;
  for (const auto& factory : factories) {
    auto shader = factory();
    auto info = shader->info();
    const tgfx::ShaderSource* source = compiling ? sources.find(info.name) : nullptr;
    if (compiling && source == nullptr) {
      std::cerr << "  ERROR: no loaded source for " << info.name << "\n";
      return 1;
    }
    auto shaderReport =
        tgfx::CompileOneShader(info, options, source, &report.variants, &report.inlineExpansions,
                               &report.profileErrorCounts);
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
      auto digest = tgfx::ComputeSourceDigest(sources, pair.first);
      std::string manifestError;
      if (!digest.ok || !tgfx::WriteBundleManifest(path, pair.first, digest, &manifestError)) {
        std::cerr << "Failed to write the manifest of " << filename << ": "
                  << (digest.ok ? manifestError : digest.error) << "\n";
        return 1;
      }
    }
  }

  if (compiling) {
    tgfx::StageCache::Get().printSummary();
  }
  return 0;
}

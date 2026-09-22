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
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include "BundleVerifier.h"
#include "BundleWriter.h"
#include "ContractChecks.h"
#include "ReflectionExtractor.h"
#include "ShaderCompiler.h"
#include "StageReport.h"
#include "gpu/shaders/PermutationRules.h"
#include "gpu/shaders/PrecompiledShader.h"
#include "gpu/shaders/level1/AtlasTextFillShader.h"
#include "gpu/shaders/level1/ComplexEllipseFillShader.h"
#include "gpu/shaders/level1/ComplexNonAARRectFillShader.h"
#include "gpu/shaders/level1/DeviceSpaceTextureShader.h"
#include "gpu/shaders/level1/EllipseFillShader.h"
#include "gpu/shaders/level1/GaussianBlur1DShader.h"
#include "gpu/shaders/level1/HairlineLineShader.h"
#include "gpu/shaders/level1/HairlineQuadShader.h"
#include "gpu/shaders/level1/MaskFillShader.h"
#include "gpu/shaders/level1/MeshFillShader.h"
#include "gpu/shaders/level1/NonAARRectFillShader.h"
#include "gpu/shaders/level1/PerlinNoiseFillShader.h"
#include "gpu/shaders/level1/PointwiseChainShader.h"
#include "gpu/shaders/level1/PointwiseDirectShader.h"
#include "gpu/shaders/level1/PointwiseTailShader.h"
#include "gpu/shaders/level1/QuadColorFillShader.h"
#include "gpu/shaders/level1/QuadTextureFillShader.h"
#include "gpu/shaders/level1/RoundStrokeRectFillShader.h"
#include "gpu/shaders/level1/ShapeInstancedFillShader.h"
#include "gpu/shaders/level1/ShapeInstancedTextureCoverageShader.h"
#include "gpu/shaders/level1/SolidColorFillShader.h"
#include "gpu/shaders/level1/TextureColorMatrixShader.h"
#include "gpu/shaders/level1/TextureFillShader.h"
#include "gpu/shaders/level1/TexturedEffectShader.h"
#include "gpu/shaders/level1/TiledTextureFillShader.h"
#include "gpu/shaders/level1/UnifiedGradientShader.h"
#include "gpu/shaders/level1/YUVTextureFillShader.h"

namespace tgfx {

struct BuildOptions {
  std::string shaderDir;
  std::string outDir;
  std::vector<std::string> backends;
  bool reportOnly = false;
  bool compress = false;
  bool audit = false;
  std::string stageReportPath;
  std::string verifyBundleDir;
  // Calibration mode for the GLES direct emission: run EmitDirectGLSLES300 on every ES
  // variant and report normalization-diff mismatches against the regenerated text, while the
  // bundle keeps storing the regenerated form. The switch to storing direct text is blocked on
  // a clean SwiftShader baseline; this keeps the transform proven in the meantime.
  bool glesDirectCheck = false;
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
  std::cerr
      << "Usage: shader_build_tool [options]\n"
      << "  --shader-dir <path>   Directory containing shader sources\n"
      << "  --out-dir <path>      Output directory for build artifacts\n"
      << "  --backends <list>     Comma-separated backend list "
         "(opengl,opengles,vulkan,metal,webgpu)\n"
      << "  --report-only         Only enumerate and report, do not compile\n"
      << "  --audit               Cross-check legacy compile lists against rule-reachable sets\n"
      << "  --stage-report <path> Write the per-stage audit report (requires a full compile)\n"
      << "  --verify-bundle <dir> Verify existing bundles in <dir> against the reachable set and\n"
      << "                        exit; checks headers, identity hash, and pool completeness\n"
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
    } else if (std::strcmp(argv[i], "--audit") == 0) {
      options->audit = true;
    } else if (std::strcmp(argv[i], "--stage-report") == 0 && i + 1 < argc) {
      options->stageReportPath = argv[++i];
    } else if (std::strcmp(argv[i], "--verify-bundle") == 0 && i + 1 < argc) {
      options->verifyBundleDir = argv[++i];
    } else if (std::strcmp(argv[i], "--compress") == 0) {
      options->compress = true;
    } else if (std::strcmp(argv[i], "--gles-direct-check") == 0) {
      options->glesDirectCheck = true;
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

// Removes 'set = <n>, ' from layout qualifiers. glslang's OpenGL target rejects the descriptor-set
// qualifier, while binding is kept (the GLSL output and reflection only rely on binding).
// The token is only a descriptor set when it appears inside a layout(...) qualifier, i.e. the
// preceding non-space character is '(' or ','. This avoids false positives on identifiers that
// merely contain the substring (e.g. "offset = ", "Subset = ").
static std::string StripDescriptorSets(std::string source) {
  std::string result;
  size_t cursor = 0;
  const std::string token = "set = ";
  while (true) {
    auto pos = source.find(token, cursor);
    if (pos == std::string::npos) {
      result += source.substr(cursor);
      break;
    }
    // Require a layout-qualifier context: the nearest non-space character before the token must be
    // '(' or ','. Otherwise it is an identifier fragment, not a descriptor set — skip it.
    size_t before = pos;
    while (before > 0 && (source[before - 1] == ' ' || source[before - 1] == '\t')) {
      --before;
    }
    bool isDescriptorSet = before > 0 && (source[before - 1] == '(' || source[before - 1] == ',');
    auto digitsStart = pos + token.size();
    auto digitsEnd = source.find_first_not_of("0123456789", digitsStart);
    if (!isDescriptorSet || digitsEnd == std::string::npos || source[digitsEnd] != ',') {
      result += source.substr(cursor, digitsStart - cursor);
      cursor = digitsStart;
      continue;
    }
    result += source.substr(cursor, pos - cursor);
    cursor = digitsEnd + 1;
    if (cursor < source.size() && source[cursor] == ' ') {
      cursor++;
    }
  }
  return result;
}

// Rewrites a preprocessed template (GLSL 450 with Vulkan-style set/binding layout qualifiers)
// into the desktop-GL 330 form the precompiled GL pipeline consumes: drop the `set = N` and
// `binding = N` layout members (330 has no descriptor-set syntax; the runtime binds the UBO at a
// fixed point and the samplers in declaration order — exactly what the spirv-cross 330 output
// relies on), lower the version header, fold macro-expression layout ids to literals, and strip
// line comments. Storing the template's own spelling instead of the regenerated one removes the
// SSA-to-source round trip, whose call-site temporaries (param/param_1) and normalized block
// names measured +79% lines on the chain interpreter — every stored GL blob the driver must
// parse and compile carries that inflation. Reflection keeps coming from the common SPIR-V
// (same template, same names, same std140 layout), so the loader contract and the ABI are
// unchanged. GLES keeps the regenerated path: its ES-300 precision form and the
// framebuffer-fetch remap are spirv-cross IR-level features with no template-side equivalent.
// Returns an empty string when the source does not carry the expected version, so the caller
// fails the variant closed instead of storing an untransformed 450 text.
//
// The macro-state machine below is a minimal conditional preprocessor: the driver's 330
// front-end rejects non-literal layout ids where the regenerated form carried folded literals
// (spirv-cross reads them back from SPIR-V), and the templates write `location = NTEX + 1` /
// `CHAIN_TEX_LOC_BASE + 0` with defines that depend on the injected permutation defines. Only
// the forms the sources actually use are supported: `#if NAME`, `#if NAME == N`, `#ifndef`,
// `#elif`, `#else`, `#endif`, `#define NAME N`, `#undef NAME` — no defined(), no arithmetic
// conditions (verified across the tree).
static std::string EmitDirectGLSL330Impl(const std::string& source, ShaderStageType stage,
                                         const std::string& versionLine, bool esDialect,
                                         bool fbfVariant);

static std::string EmitDirectGLSL330(const std::string& source, ShaderStageType stage) {
  return EmitDirectGLSL330Impl(source, stage, "#version 330", false, false);
}

// ES-300 direct emission (the GLES sibling of EmitDirectGLSL330): the same strip/fold/comment
// transforms on the preprocessed template, plus the four ES-specific rewrites that the
// regenerated ES-300 output has shipped in:
//   1. `#version 300 es` + the two default precision lines;
//   2. every float-family declaration decorated `highp` (the regenerated form is uniformly
//      highp-decorated — 45k occurrences, no exceptions — so a blanket rule reproduces it; ES
//      defaults to mediump and many devices implement it as fp16, so the decoration is a
//      numerical contract, not cosmetics);
//   3. fbf variants (HAS_XP == 2): subpassInput declaration and subpassLoad() replaced with the
//      GL_EXT_shader_framebuffer_fetch dialect — the inout fragment output is the dst read;
//   4. `out` fragment output becomes `inout` on fbf variants only.
// CALIBRATION-ONLY MODE (option `--gles-direct-check`): the transform runs and each variant is
// diffed against the regenerated ES-300 text (normalized: whitespace, the spirv-cross block
// rename `_NNNN.`, param_N temporaries folded); mismatches are reported and counted as errors,
// but the bundle still stores the REGENERATED text. This proves the transform without switching
// the stored artifact on top of a test baseline (SwiftShader) that currently has failures —
// the switch happens only when that baseline is clean, by removing the calibration guard.
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

  std::map<std::string, int64_t> macros;
  // Conditional stack: for each open #if, the parent's activity at entry, whether any branch
  // was taken yet, and whether the current branch is active. A branch inside a dead outer
  // region stays dead no matter its own condition — the earlier parentActive derivation
  // (`branchTaken.size() > 1`) was always-true and let dead-outer/live-inner #else blocks
  // execute their #defines, polluting the macro table.
  std::vector<bool> branchActive;
  std::vector<bool> branchParent;
  std::vector<bool> branchTaken;
  auto activeNow = [&branchActive]() { return branchActive.empty() || branchActive.back(); };
  std::string out;
  out.reserve(result.size() / 2);

  auto macroValue = [&macros](const std::string& name) -> int64_t {
    auto it = macros.find(name);
    return it == macros.end() ? 0 : it->second;
  };
  auto evalCondition = [&](const std::string& expr) -> int64_t {
    // `NAME` (truthiness) or `NAME == N` / `NAME != N`; unknown names evaluate to 0 like a
    // real preprocessor.
    auto eq = expr.find("==");
    auto ne = expr.find("!=");
    if (eq != std::string::npos || ne != std::string::npos) {
      auto opPos = eq != std::string::npos ? eq : ne;
      auto left = expr.substr(0, opPos);
      auto right = expr.substr(expr.find_first_not_of("= ", opPos));
      auto trim = [](std::string s) {
        auto a = s.find_first_not_of(" \t\r");
        auto b = s.find_last_not_of(" \t\r");
        return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
      };
      left = trim(left);
      right = trim(right);
      int64_t rhs = 0;
      try {
        rhs = std::stoll(right);
      } catch (...) {
        rhs = macroValue(right);
      }
      int64_t lhs = isdigit(left[0]) ? std::stoll(left) : macroValue(left);
      return eq != std::string::npos ? (lhs == rhs) : (lhs != rhs);
    }
    auto trim = [](std::string s) {
      auto a = s.find_first_not_of(" \t\r");
      auto b = s.find_last_not_of(" \t\r");
      return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
    };
    auto name = trim(expr);
    return isdigit(name[0]) ? std::stoll(name) : macroValue(name);
  };

  size_t cursor = 0;
  while (cursor < result.size()) {
    auto lineEnd = result.find('\n', cursor);
    auto line =
        result.substr(cursor, lineEnd == std::string::npos ? std::string::npos : lineEnd - cursor);
    bool active = activeNow();
    auto directive = line.find_first_not_of(" \t");
    if (directive != std::string::npos && line[directive] == '#') {
      auto head = line.substr(directive);
      if (head.rfind("#if ", 0) == 0 || head.rfind("#if\t", 0) == 0) {
        bool parent = activeNow();
        bool taken = parent && evalCondition(head.substr(3)) != 0;
        branchActive.push_back(taken);
        branchParent.push_back(parent);
        branchTaken.push_back(taken);
      } else if (head.rfind("#ifndef ", 0) == 0) {
        auto name = head.substr(8);
        auto trim = name.find_first_not_of(" \t\r");
        name = name.substr(trim);
        bool parent = activeNow();
        bool taken = parent && macros.find(name) == macros.end();
        branchActive.push_back(taken);
        branchParent.push_back(parent);
        branchTaken.push_back(taken);
      } else if (head.rfind("#ifdef ", 0) == 0) {
        auto name = head.substr(7);
        auto trim = name.find_first_not_of(" \t\r");
        name = name.substr(trim);
        bool parent = activeNow();
        bool taken = parent && macros.find(name) != macros.end();
        branchActive.push_back(taken);
        branchParent.push_back(parent);
        branchTaken.push_back(taken);
      } else if (head.rfind("#elif ", 0) == 0) {
        if (!branchActive.empty()) {
          bool taken =
              branchParent.back() && !branchTaken.back() && evalCondition(head.substr(5)) != 0;
          branchActive.back() = taken;
          branchTaken.back() = branchTaken.back() || taken;
        }
      } else if (head.rfind("#else", 0) == 0) {
        if (!branchActive.empty()) {
          branchActive.back() = branchParent.back() && !branchTaken.back();
          branchTaken.back() = true;
        }
      } else if (head.rfind("#endif", 0) == 0) {
        if (!branchActive.empty()) {
          branchActive.pop_back();
          branchParent.pop_back();
          branchTaken.pop_back();
        }
      } else if (activeNow() && head.rfind("#define ", 0) == 0) {
        auto body = head.substr(8);
        auto nameEnd = body.find_first_of(" \t");
        if (nameEnd != std::string::npos) {
          auto name = body.substr(0, nameEnd);
          auto valueStart = body.find_first_not_of(" \t", nameEnd);
          if (valueStart != std::string::npos) {
            try {
              int64_t value = std::stoll(body.substr(valueStart));
              macros[name] = value;
            } catch (...) {
              // Non-numeric macro (e.g. CHAIN_LEAF_SAMPLER): not a layout-id input.
            }
          } else {
            macros.erase(name);
          }
        }
      } else if (active && head.rfind("#undef ", 0) == 0) {
        macros.erase(head.substr(7));
      }
      // Directives stay in the output verbatim: the driver's own preprocessor consumes
      // #version/#define/#if pairs — the state machine above only mirrors it to know the
      // macro values at each layout id.
      auto directiveComment = line.find("//");
      out += directiveComment == std::string::npos ? line : line.substr(0, directiveComment);
      out += '\n';
      cursor = lineEnd == std::string::npos ? result.size() : lineEnd + 1;
      continue;
    }
    // Strip the line comment, then drop the line when only whitespace remains. Dead
    // preprocessor branches keep their lines verbatim (the driver's preprocessor removes
    // them), but their comments are stripped too — they never reach the driver.
    auto comment = line.find("//");
    auto code = comment == std::string::npos ? line : line.substr(0, comment);
    if (code.find_first_not_of(" \t\r") == std::string::npos) {
      cursor = lineEnd == std::string::npos ? result.size() : lineEnd + 1;
      continue;
    }
    if (active) {
      // Track live defines only; dead-branch defines never reach the driver.
    }
    // Rewrite layout(...) lists on every line — dead preprocessor branches keep their text
    // for the driver's preprocessor to drop, but rewriting them too is harmless and keeps the
    // form assertion meaningful (a surviving set/binding member is a strip miss, whether or
    // not the branch is live).
    {
      auto open = code.find("layout(");
      while (open != std::string::npos) {
        auto close = code.find(')', open);
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
                // Fold `location = <expr>` to a literal using the current macro state.
                auto expr = member.substr(eq + 2);
                int64_t folded = 0;
                bool ok = true;
                // Sum/difference of macros and integers — the only forms the sources use.
                size_t partCursor = 0;
                bool negate = false;
                while (partCursor < expr.size()) {
                  auto op = expr.find_first_of("+-", partCursor == 0 ? 0 : partCursor);
                  auto part = expr.substr(
                      partCursor, op == std::string::npos ? std::string::npos : op - partCursor);
                  auto partWs = part.find_first_not_of(" \t\r");
                  if (partWs != std::string::npos) {
                    auto partEnd = part.find_last_not_of(" \t\r");
                    part = part.substr(partWs, partEnd - partWs + 1);
                    int64_t v = 0;
                    if (isdigit(part[0])) {
                      try {
                        v = std::stoll(part);
                      } catch (...) {
                        ok = false;
                      }
                    } else if (isalpha(part[0]) || part[0] == '_') {
                      v = macroValue(part);
                    } else {
                      ok = false;
                    }
                    folded += negate ? -v : v;
                  }
                  if (op == std::string::npos) break;
                  negate = expr[op] == '-';
                  partCursor = op + 1;
                }
                if (ok) {
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
  return EmitDirectGLSL330Impl(source, stage, "#version 300 es", true, fbfVariant);
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
        // Recurse so included files may include others (e.g. xp_porter_duff.inc pulling in the
        // shared blend math). Include guards in the sources prevent duplicate definitions;
        // intentionally guardless files (the slot bind/unbind headers) are included repeatedly by
        // design, so no visited-set is applied.
        result += ResolveIncludes(includeContent, baseDir) + "\n";
        continue;
      }
    }
    result += line + "\n";
  }
  return result;
}

// Process-wide reuse of GLSL-to-SPIR-V compilation and reflection extraction results. The common
// compile loop runs before the per-backend emission passes, so what the cache actually saves is
// the repeats WITHIN that loop: multiple frag variants sharing one vert permutation, revisit
// patterns in later passes, and the optimized re-compiles. (The earlier claim that it collapses
// N per-backend compilations into one was wrong: the common compiles already run once, outside
// the backend loop.) Backend-specific conversions (MSL/metallib, WGSL, direct GL emission) each
// run in exactly one backend pass and are not cached. The cache key must never reuse a
// permutation index across different inputs: it always carries the shader name, mirroring the
// rule that a permutation index is only meaningful within its family.
struct SpirvCacheEntry {
  bool success = false;
  std::vector<uint32_t> spirv;
  std::string error;
};
static std::map<std::tuple<std::string, uint32_t, int, bool, bool>, SpirvCacheEntry> spirvCache;

static const SpirvCacheEntry& CompileGLSLShared(const std::string& source, ShaderStageType stage,
                                                const std::string& shaderName,
                                                uint32_t variantIndex, bool optimize,
                                                bool openGLEnv) {
  auto key =
      std::make_tuple(shaderName, variantIndex, static_cast<int>(stage), optimize, openGLEnv);
  auto it = spirvCache.find(key);
  if (it == spirvCache.end()) {
    auto result = CompileGLSL(source, stage, shaderName, variantIndex, optimize, openGLEnv);
    SpirvCacheEntry entry;
    entry.success = result.success;
    entry.spirv = std::move(result.spirv);
    entry.error = std::move(result.error);
    it = spirvCache.emplace(std::move(key), std::move(entry)).first;
  }
  return it->second;
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

  // The compile list comes from the matcher rules' reachable sets (the Compose single source of
  // truth in PermutationRules.cpp), enumerated at build time. This replaces the former cartesian
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

    // A TEXTURE_KIND=1 variant declares sampler2DRect, whose SPIR-V (OpTypeImage Dim=Rect) is
    // invalid under Vulkan semantics. It compiles under OpenGL semantics and only enters the
    // opengl bundle.
    bool rectVariant = info.fragDomain.valueOf(fi, "TEXTURE_KIND") == 1;

    auto vertDefines = vertDomain.defineListFor(vi);
    auto fragDefines = fragDomain.defineListFor(fi);

    // Compile vertex shader. The shared cache also covers the multiple-frag-per-vert reuse the
    // old per-backend vertCache handled, plus the cross-backend repeats.
    const auto& vertResult =
        CompileGLSLShared(PrependDefines(vertSource, vertDefines), ShaderStageType::Vertex,
                          info.name, vi, false, false);
    if (!vertResult.success) {
      std::cerr << "  " << vertResult.error << "\n";
      report.errorCount++;
      RecordCommonArtifactError(profileErrorCounts);
      continue;
    }
    const auto* vertSpirv = &vertResult.spirv;

    auto expandedFrag = PrependDefines(fragSource, fragDefines);
    if (rectVariant) {
      // glslang's OpenGL target rejects 'descriptor set' layout qualifiers, so strip the
      // 'set = N, ' fragment while keeping binding (which the reflection and GLSL output use).
      expandedFrag = StripDescriptorSets(std::move(expandedFrag));
    }
    const auto& fragResult = CompileGLSLShared(expandedFrag, ShaderStageType::Fragment, info.name,
                                               fi, false, rectVariant);
    if (!fragResult.success) {
      std::cerr << "  " << fragResult.error << "\n";
      report.errorCount++;
      RecordCommonArtifactError(profileErrorCounts);
      continue;
    }

    // Extract reflection from SPIR-V (shared across backend passes, like the compiles above).
    const auto& reflection =
        ExtractReflectionShared(info.name, vi, fi, *vertSpirv, fragResult.spirv);
    // Counted only here — after both stages compiled successfully — so compiledCount is the
    // true "compiled" figure, distinct from rawCount (the reachable-permutation total) and
    // independent of backend-level exclusions counted below.
    report.compiledCount++;

    for (const auto& backend : options.backends) {
      // Backend-specific exclusions (RECT, WebGPU FBF) share one source of truth with the
      // closure verifier; see PermutationCompilesForBackend.
      if (!PermutationCompilesForBackend(info, vi, fi, backend)) {
        continue;
      }
      std::vector<uint8_t> vertBlob;
      std::vector<uint8_t> fragBlob;

      if (backend == "vulkan") {
        // Re-compile with optimization for smaller SPIR-V output.
        auto expandedVertOpt = PrependDefines(vertSource, vertDefines);
        auto vertOpt =
            CompileGLSLShared(expandedVertOpt, ShaderStageType::Vertex, info.name, vi, true, false);
        auto expandedFragOpt = PrependDefines(fragSource, fragDefines);
        auto fragOpt = CompileGLSLShared(expandedFragOpt, ShaderStageType::Fragment, info.name, fi,
                                         true, false);
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
        // WebGPU needs its own GLSL form (separated texture/sampler bindings), so re-expand and
        // recompile like the vulkan branch instead of reusing the combined-sampler SPIR-V.
        auto expandedVertWgsl = PrependDefines(vertSource, vertDefines);
        auto expandedFragWgsl = PrependDefines(fragSource, fragDefines);
        auto wgslVert = CompileGLSLToWGSL(expandedVertWgsl, ShaderStageType::Vertex, info.name, vi);
        auto wgslFrag =
            CompileGLSLToWGSL(expandedFragWgsl, ShaderStageType::Fragment, info.name, fi);
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
            std::cerr << "  direct-GL 330 emission failed (missing #version 450 header) for "
                      << info.name << " [vert=" << vi << " frag=" << fi << "]\n";
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
          // preceded by '(' or ','), the same test StripDescriptorSets uses.
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
            // Runs on the un-preprocessed text, so only transform-level leaks are checkable
            // here: a surviving set/binding qualifier (the strip missed a form) or a missing
            // version rewrite. Vulkan-only syntax inside a `#if HAS_XP == 2` block is NOT a
            // leak — the block is dead for every desktop variant, and the live-block routing
            // (PermutationCompilesForBackend excludes XP=2 from opengl) is audited elsewhere.
            auto setPos = hasLayoutMember(text, "set =");
            auto bindingPos = hasLayoutMember(text, "binding =");
            if (setPos >= 0 || bindingPos >= 0 || text.find("#version 330") == std::string::npos) {
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
          // Calibration for the direct ES-300 emission (see BuildOptions::glesDirectCheck).
          // A raw text diff against the regenerated form is not possible: the direct text
          // keeps its #if/#define blocks for the driver's preprocessor while the regenerated
          // text has them resolved away, so the two can never be textually equal. What CAN be
          // proven at build time is the transform's own correctness: the form assertions
          // (version, no set/binding, fbf substitutions) fail closed on any transform leak,
          // and the size report quantifies the win pending the storage switch.
          bool fbfVariant = info.fragDomain.valueOf(fi, "HAS_XP") == 2;
          if (options.glesDirectCheck) {
            auto directVert = EmitDirectGLSLES300(PrependDefines(vertSource, vertDefines),
                                                  ShaderStageType::Vertex, false);
            auto directFrag = EmitDirectGLSLES300(PrependDefines(fragSource, fragDefines),
                                                  ShaderStageType::Fragment, fbfVariant);
            auto assertDirectESForm = [&](const std::string& text, const char* stage) -> bool {
              if (text.find("#version 300 es") == std::string::npos ||
                  text.find("precision mediump float;") == std::string::npos) {
                std::cerr << "  GLES direct-ES form assertion failed (" << stage << ") for "
                          << info.name << " [vert=" << vi << " frag=" << fi << "]\n";
                report.errorCount++;
                RecordBackendArtifactError(backend, profileErrorCounts);
                return false;
              }
              // A surviving layout member means the strip missed a form; the same
              // layout-context test the desktop path uses (preceded by '(' or ',').
              auto hasLayoutMember = [](const std::string& t, const std::string& member) {
                std::string token = member + " ";
                size_t at = 0;
                while ((at = t.find(token, at)) != std::string::npos) {
                  size_t before = at;
                  while (before > 0 && (t[before - 1] == ' ' || t[before - 1] == '\t')) {
                    --before;
                  }
                  if (before > 0 && (t[before - 1] == '(' || t[before - 1] == ',')) {
                    return true;
                  }
                  ++at;
                }
                return false;
              };
              if (hasLayoutMember(text, "set =") || hasLayoutMember(text, "binding =")) {
                std::cerr << "  GLES direct-ES form assertion failed (" << stage
                          << ", surviving qualifier) for " << info.name << " [vert=" << vi
                          << " frag=" << fi << "]\n";
                report.errorCount++;
                RecordBackendArtifactError(backend, profileErrorCounts);
                return false;
              }
              if (fbfVariant && stage == std::string("frag")) {
                if (text.find("subpassInput") != std::string::npos ||
                    text.find("subpassLoad") != std::string::npos) {
                  std::cerr << "  GLES direct-ES fbf substitution incomplete for " << info.name
                            << " [vert=" << vi << " frag=" << fi << "]\n";
                  report.errorCount++;
                  RecordBackendArtifactError(backend, profileErrorCounts);
                  return false;
                }
              }
              return true;
            };
            if (assertDirectESForm(directVert, "vert") && assertDirectESForm(directFrag, "frag")) {
              std::cerr << "  [gles-direct] " << info.name << " v" << vi << " f" << fi
                        << (fbfVariant ? " fbf" : "") << ": direct "
                        << (directVert.size() + directFrag.size()) << " B\n";
            }
          }
          // Direct ES-300 storage (the same rationale as desktop GL, see EmitDirectGLSL330):
          // the regenerated form's call-site temporaries (param/param_1) and normalized block
          // names measured +45% pool bytes; the direct form removes them. Verified on the
          // SwiftShader suite — 729 green including byte-parity on every compilable variant —
          // after the precision/version fixes (see the ledger).
          auto dv = EmitDirectGLSLES300(PrependDefines(vertSource, vertDefines),
                                        ShaderStageType::Vertex, false);
          auto df = EmitDirectGLSLES300(PrependDefines(fragSource, fragDefines),
                                        ShaderStageType::Fragment, fbfVariant);
          if (dv.empty() || df.empty()) {
            std::cerr << "  direct-ES emission failed (missing #version 450) for " << info.name
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

  if (options.audit) {
    return tgfx::RunAuditMode();
  }

  if (!options.verifyBundleDir.empty()) {
    return tgfx::VerifyBundles(options.verifyBundleDir);
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
  if (!options.reportOnly && !options.shaderDir.empty()) {
    contractErrors += tgfx::ValidateChainOpCodes(options.shaderDir);
    contractErrors += tgfx::ValidatePointwiseOpCodes(options.shaderDir);
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
    }
  }

  return 0;
}

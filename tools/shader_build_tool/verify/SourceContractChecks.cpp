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
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. See the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "verify/ContractChecks.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include "core/DiagnosticCodes.h"
#include "gpu/aot/AOTResourceProfile.h"
#include "gpu/aot/bind/AOTParamRegistry.h"
#include "gpu/shaders/KernelContract.h"
#include "gpu/shaders/PrecompiledShader.h"
#include "gpu/shaders/contract/SpecContract.h"
#include "verify/ContractCheckSupport.h"

namespace tgfx {

namespace {

using contract_check::Report;

}  // namespace

static bool IsIdentifierChar(char ch) {
  return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
         ch == '_';
}

static bool EndsWith(const std::string& text, const std::string& suffix) {
  return text.size() >= suffix.size() &&
         text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Names that end in "Shader" but are not tgfx shader families.
static bool IsExternalShaderName(const std::string& name) {
  return name == "SwiftShader";
}

size_t ValidateShaderSourceReferences(const std::string& shaderDir,
                                      const std::vector<std::string>& registeredShaders,
                                      Diagnostics* diagnostics) {
  namespace fs = std::filesystem;
  std::error_code code;
  std::set<std::string> existingFiles;
  std::vector<fs::path> sourcePaths;
  for (fs::recursive_directory_iterator it(shaderDir, code), end; !code && it != end;
       it.increment(code)) {
    if (!it->is_regular_file()) {
      continue;
    }
    existingFiles.insert(it->path().filename().string());
    auto extension = it->path().extension().string();
    if (extension == ".inc" || extension == ".frag" || extension == ".vert") {
      sourcePaths.push_back(it->path());
    }
  }
  if (code || sourcePaths.empty()) {
    Report(diagnostics, Severity::Unverifiable, DiagnosticCode::VerifyNotApplicable, "", "", -1, -1,
           "cannot list shader sources under " + shaderDir);
    return 1;
  }
  const std::set<std::string> registered(registeredShaders.begin(), registeredShaders.end());
  const std::string fileSuffixes[] = {".inc", ".frag", ".vert"};
  size_t errors = 0;
  for (const auto& path : sourcePaths) {
    std::ifstream file(path);
    std::stringstream buffer;
    buffer << file.rdbuf();
    const auto text = buffer.str();
    const auto fileName = path.filename().string();
    size_t position = 0;
    while (position < text.size()) {
      if (!IsIdentifierChar(text[position])) {
        ++position;
        continue;
      }
      size_t tokenEnd = position;
      while (tokenEnd < text.size() && IsIdentifierChar(text[tokenEnd])) {
        ++tokenEnd;
      }
      const auto token = text.substr(position, tokenEnd - position);
      // A "<module>_uniforms.inc" or "pointwise_*.inc" mention is a placeholder, not a file name.
      const bool placeholder =
          position > 0 && (text[position - 1] == '>' || text[position - 1] == '*');
      for (const auto& suffix : fileSuffixes) {
        if (!placeholder && text.compare(tokenEnd, suffix.size(), suffix) == 0 &&
            (tokenEnd + suffix.size() >= text.size() ||
             !IsIdentifierChar(text[tokenEnd + suffix.size()])) &&
            existingFiles.count(token + suffix) == 0) {
          Report(diagnostics, Severity::Error, DiagnosticCode::VerifySourceFile, "", "", -1, -1,
                 fileName + " refers to " + token + suffix + ", which does not exist under " +
                     shaderDir);
          ++errors;
        }
      }
      if (EndsWith(token, "Shader") && token.size() > 6 && token[0] >= 'A' && token[0] <= 'Z' &&
          !IsExternalShaderName(token) && registered.count(token) == 0) {
        Report(diagnostics, Severity::Error, DiagnosticCode::VerifySourceFamily, "", "", -1, -1,
               fileName + " refers to shader family " + token +
                   ", which is not registered (removed or merged into another family?)");
        ++errors;
      }
      position = tokenEnd;
    }
  }
  return errors;
}

// Reflection-level invariants the runtime loader enforces on read: catching them at build time
// keeps a broken writer from shipping bundles that the loader would reject (or worse, accept with
// gaps in the sampler binding table).

// of them, so they join the allowed set of the macro-name rule.
struct DimensionNameVisitor {
  const char* operator()(const PermutationBool& dimension) const {
    return dimension.defineName;
  }
  const char* operator()(const PermutationEnum& dimension) const {
    return dimension.defineName;
  }
  const char* operator()(const PermutationInt& dimension) const {
    return dimension.defineName;
  }
};

// The macro-name rule: every identifier a preprocessor condition references must be a family
// permutation dimension, a define in the shader sources, or a whitelisted builtin. A typo
// evaluates silently to zero and compiles a wrong variant, so it fails the build instead.
// Two further invariants ride the same scan:
//   1. A kernel file must not carry guard defaults (`#ifndef X` / `#define X 0`): every build
//      prepends the variant's dimension defines before the source, so a guard is inert and only
//      drifts from the family's Dim declaration (the defaults live there now). MAX_SIGMA is the
//      one fixed compile-time constant that still uses the guard pattern, whitelisted by name.
//   2. A declared dimension no preprocessor condition ever references compiles identical code
//      for every value and only multiplies the bundle, so it fails the build as dead.
size_t ValidateMacroNames(const std::string& shaderDir, Diagnostics* diagnostics) {
  size_t errors = 0;
  std::set<std::string> declaredDimensions;
  // The dimensions whose sources this scan actually reads: the directory may carry only part of
  // the registered families (a fixture, a partial copy), and demanding references for dimensions
  // whose sources are not here would report every one of them as dead.
  std::set<std::string> checkedDimensions;
  for (const auto& factory : ShaderRegistry::All()) {
    const auto shader = factory();
    // The domains are members of the info value, so a local keeps them alive through the loops:
    // ranging over shader->info().vertDomain.getDimensions() directly would hold a reference into
    // a destroyed temporary.
    const auto info = shader->info();
    const bool present =
        std::filesystem::exists(shaderDir + "/" + info.vertexFile) ||
        std::filesystem::exists(shaderDir + "/" + info.fragmentFile);
    for (const auto& dimension : info.vertDomain.getDimensions()) {
      auto name = std::visit(DimensionNameVisitor(), dimension);
      declaredDimensions.insert(name);
      if (present) {
        checkedDimensions.insert(name);
      }
    }
    for (const auto& dimension : info.fragDomain.getDimensions()) {
      auto name = std::visit(DimensionNameVisitor(), dimension);
      declaredDimensions.insert(name);
      if (present) {
        checkedDimensions.insert(name);
      }
    }
  }
  std::set<std::string> allowed = declaredDimensions;
  allowed.insert("GL_ES");
  allowed.insert("__VERSION__");

  std::vector<std::string> files;
  for (std::filesystem::recursive_directory_iterator it(shaderDir), end; it != end; ++it) {
    if (!it->is_regular_file()) {
      continue;
    }
    const auto extension = it->path().extension().string();
    if (extension != ".inc" && extension != ".frag" && extension != ".vert") {
      continue;
    }
    files.push_back(it->path().string());
  }
  const std::regex definePattern("^[ \t]*#[ \t]*define[ \t]+([A-Za-z_][A-Za-z0-9_]*)");
  const std::regex ifndefPattern("^[ \t]*#[ \t]*ifndef[ \t]+([A-Za-z_][A-Za-z0-9_]*)");
  const std::regex conditionPattern(
      "^[ \\t]*#[ \\t]*(?:ifdef|ifndef|elif|if)\\b[ \\t]*(.*)$");
  const std::regex identifierPattern("([A-Za-z_][A-Za-z0-9_]*)");
  static const std::set<std::string> guardDefaultWhitelist = {"MAX_SIGMA"};
  std::set<std::string> usedInConditions;
  // Pass one: the defines of every file join the allowed set, because an includer sees them —
  // with the kernel-file guard-default exception explained above.
  for (const auto& file : files) {
    const auto relative = std::filesystem::relative(file, shaderDir).generic_string();
    const bool kernelFile = relative.rfind("kernels/", 0) == 0;
    std::ifstream stream(file);
    std::string line;
    std::string pendingIfndef;
    while (std::getline(stream, line)) {
      std::smatch match;
      if (std::regex_search(line, match, ifndefPattern)) {
        pendingIfndef = match[1].str();
        continue;
      }
      // Blank lines and line comments do not break the #ifndef/#define adjacency: a hand-written
      // guard may separate the two by a comment. Any other line (an #endif, another directive,
      // code) closes the guard, so a later unrelated #define of the same name is not mistaken
      // for one.
      const auto firstChar = line.find_first_not_of(" \t");
      const auto content = firstChar == std::string::npos ? std::string() : line.substr(firstChar);
      if (content.empty() || content.rfind("//", 0) == 0) {
        continue;
      }
      if (std::regex_search(line, match, definePattern)) {
        const auto name = match[1].str();
        if (kernelFile && name == pendingIfndef && !guardDefaultWhitelist.count(name)) {
          Report(diagnostics, Severity::Error, DiagnosticCode::VerifyMacroUnknown, "", "", -1, -1,
                 relative + " defaults '" + name +
                     "' with the #ifndef pattern. The build prepends every declared dimension's "
                     "define before the source, so a kernel guard is inert and only drifts from "
                     "the family's Dim declaration; remove the #ifndef/#define/#endif block "
                     "(whitelist: MAX_SIGMA, a fixed compile-time constant)");
          ++errors;
        } else {
          allowed.insert(name);
        }
      }
      pendingIfndef.clear();
    }
  }
  // Pass two: every identifier a condition references must be known. The TGFX_ and __ prefixes
  // are the toolchain's own macros, kept out of the per-family bookkeeping.
  for (const auto& file : files) {
    const auto relative = std::filesystem::relative(file, shaderDir).generic_string();
    std::ifstream stream(file);
    std::string line;
    int lineNumber = 0;
    while (std::getline(stream, line)) {
      ++lineNumber;
      std::smatch match;
      if (!std::regex_search(line, match, conditionPattern)) {
        continue;
      }
      std::sregex_iterator identifier(match[1].first, match[1].second, identifierPattern);
      for (std::sregex_iterator end; identifier != end; ++identifier) {
        const auto name = (*identifier)[1].str();
        if (name == "defined" || name.rfind("TGFX_", 0) == 0 || name.rfind("__", 0) == 0 ||
            allowed.count(name) > 0) {
          usedInConditions.insert(name);
          continue;
        }
        Report(diagnostics, Severity::Error, DiagnosticCode::VerifyMacroUnknown, "", "", -1, -1,
               relative + ":" + std::to_string(lineNumber) + " conditions on '" + name +
                   "', which no family dimension or source define declares");
        ++errors;
      }
    }
  }
  for (const auto& dimension : checkedDimensions) {
    if (usedInConditions.count(dimension) == 0) {
      Report(diagnostics, Severity::Error, DiagnosticCode::VerifyMacroUnknown, "", "", -1, -1,
             "dimension '" + dimension +
                 "' is declared by a family but no preprocessor condition references it; every "
                 "value compiles identical code, so the dimension only multiplies the bundle");
      ++errors;
    }
  }
  return errors;
}



// The pointwise dispatch rule: the shared GLSL dispatch must cover the op list. An operator needs
// an `op == OP_<NAME>` branch or a `tgfx_pointwise_exempt <Name>: <reason>` entry (the reason is
// mandatory: an exemption is a design decision, and the file is where it is written down). A
// branch or exemption naming no listed operator fails too, so renaming or removing an operator
// cannot leave a dead arm or a stale note behind.
size_t ValidatePointwiseDispatch(const std::string& shaderDir, Diagnostics* diagnostics) {
  const std::string relative = "operator/pointwise_color_dispatch.inc";
  const auto path = std::filesystem::path(shaderDir) / relative;
  std::ifstream stream(path);
  if (!stream) {
    Report(diagnostics, Severity::Error, DiagnosticCode::VerifyPointwiseDispatch, "", "", -1, -1,
           relative + " is missing; the pointwise interpreters include the shared dispatch");
    return 1;
  }
  const std::string text((std::istreambuf_iterator<char>(stream)),
                         std::istreambuf_iterator<char>());
  const std::regex branchPattern("op[ \\t]*==[ \\t]*OP_([A-Z0-9_]+)");
  const std::regex exemptionPattern(
      "tgfx_pointwise_exempt[ \\t]+([A-Za-z_][A-Za-z0-9_]*)[ \\t]*:[ \\t]*([^\\r\\n]*)");
  std::set<std::string> dispatched;
  for (std::sregex_iterator it(text.begin(), text.end(), branchPattern), end; it != end; ++it) {
    dispatched.insert((*it)[1].str());
  }
  std::map<std::string, std::string> exemptions;
  for (std::sregex_iterator it(text.begin(), text.end(), exemptionPattern), end; it != end; ++it) {
    exemptions.emplace((*it)[1].str(), (*it)[2].str());
  }
  size_t errors = 0;
  std::set<std::string> listed;
  for (const auto& name : Op::PointwiseOpNames()) {
    listed.insert(name);
    if (dispatched.count(Op::DefineName(name)) != 0) {
      continue;
    }
    const auto exemption = exemptions.find(name);
    if (exemption == exemptions.end()) {
      Report(diagnostics, Severity::Error, DiagnosticCode::VerifyPointwiseDispatch, "", "", -1,
             -1,
             relative + " has no dispatch branch for operator '" + name +
                 "' and no tgfx_pointwise_exempt entry; add a branch or state why the call "
                 "sites dispatch it");
      ++errors;
      continue;
    }
    if (exemption->second.find_first_not_of(" \t") == std::string::npos) {
      Report(diagnostics, Severity::Error, DiagnosticCode::VerifyPointwiseDispatch, "", "", -1,
             -1,
             "operator '" + name +
                 "' is exempt from the shared dispatch without a reason; state why on its "
                 "tgfx_pointwise_exempt line");
      ++errors;
    }
  }
  for (const auto& define : dispatched) {
    bool known = false;
    for (const auto& name : listed) {
      if (Op::DefineName(name) == define) {
        known = true;
        break;
      }
    }
    if (!known) {
      Report(diagnostics, Severity::Error, DiagnosticCode::VerifyPointwiseDispatch, "", "", -1,
             -1,
             "dispatch branch 'OP_" + define +
                 "' names no operator of the op list; remove the branch or restore the operator");
      ++errors;
    }
  }
  for (const auto& entry : exemptions) {
    if (listed.count(entry.first) == 0) {
      Report(diagnostics, Severity::Error, DiagnosticCode::VerifyPointwiseDispatch, "", "", -1,
             -1,
             "exemption for '" + entry.first +
                 "' names no operator of the op list; remove the entry");
      ++errors;
    }
  }
  return errors;
}



namespace {

// Strips // and /* */ comments, keeping newlines so a scan cannot match inside a comment.
std::string StripSourceComments(const std::string& source) {
  std::string result;
  result.reserve(source.size());
  bool lineComment = false;
  bool blockComment = false;
  for (size_t index = 0; index < source.size(); ++index) {
    const char current = source[index];
    const char next = index + 1 < source.size() ? source[index + 1] : '\0';
    if (lineComment) {
      if (current == '\n') {
        lineComment = false;
        result.push_back(current);
      }
      continue;
    }
    if (blockComment) {
      if (current == '*' && next == '/') {
        blockComment = false;
        ++index;
      } else if (current == '\n') {
        result.push_back(current);
      }
      continue;
    }
    if (current == '/' && next == '/') {
      lineComment = true;
      ++index;
      continue;
    }
    if (current == '/' && next == '*') {
      blockComment = true;
      ++index;
      continue;
    }
    result.push_back(current);
  }
  return result;
}

bool IsScanIdentifierChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

}  // namespace

// The reader rule: a file may read the SPEC_ accessors only if it declares
// `#pragma tgfx_spec_reader`. The tool expands includes before compiling, so the check runs on
// the raw sources where the file boundary still exists; a file that joins the specialization
// surface silently would change what a spec-table edit touches without any reviewer seeing it.
size_t ValidateSpecReaders(const std::string& shaderDir, Diagnostics* diagnostics) {
  size_t errors = 0;
  for (std::filesystem::recursive_directory_iterator it(shaderDir), end; it != end; ++it) {
    if (!it->is_regular_file()) {
      continue;
    }
    const auto extension = it->path().extension().string();
    if (extension != ".inc" && extension != ".frag" && extension != ".vert") {
      continue;
    }
    std::ifstream stream(it->path());
    const std::string source((std::istreambuf_iterator<char>(stream)),
                             std::istreambuf_iterator<char>());
    const auto code = StripSourceComments(source);
    bool readsSpec = false;
    for (size_t index = 0; index + 5 <= code.size(); ++index) {
      if (code.compare(index, std::strlen(SpecContract::FieldAccessorPrefix),
                     SpecContract::FieldAccessorPrefix) != 0) {
        continue;
      }
      if (index > 0 && IsScanIdentifierChar(code[index - 1])) {
        continue;
      }
      readsSpec = true;
      break;
    }
    if (!readsSpec || source.find("#pragma tgfx_spec_reader") != std::string::npos) {
      continue;
    }
    Report(diagnostics, Severity::Error, DiagnosticCode::SpecReader, "", "", -1, -1,
           std::filesystem::relative(it->path(), shaderDir).generic_string() +
               " reads the SPEC_ accessors without declaring itself a reader; add "
               "#pragma tgfx_spec_reader or move the read into a declared reader");
    ++errors;
  }
  return errors;
}


}  // namespace tgfx

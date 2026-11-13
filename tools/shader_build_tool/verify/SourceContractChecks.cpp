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

#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <variant>
#include <vector>
#include "core/DiagnosticCodes.h"
#include "gpu/aot/AOTResourceProfile.h"
#include "gpu/shaders/Families.h"
#include "gpu/shaders/FamilyDomains.h"
#include "gpu/shaders/KernelContract.h"
#include "gpu/shaders/PrecompiledShader.h"
#include "gpu/shaders/contract/SpecContract.h"
#include "verify/ContractCheckSupport.h"
#include "verify/ContractChecks.h"

namespace tgfx {

namespace {

using contract_check::Report;

}  // namespace
;
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
      Report(diagnostics, Severity::Error, DiagnosticCode::VerifyPointwiseDispatch, "", "", -1, -1,
             relative + " has no dispatch branch for operator '" + name +
                 "' and no tgfx_pointwise_exempt entry; add a branch or state why the call "
                 "sites dispatch it");
      ++errors;
      continue;
    }
    if (exemption->second.find_first_not_of(" \t") == std::string::npos) {
      Report(diagnostics, Severity::Error, DiagnosticCode::VerifyPointwiseDispatch, "", "", -1, -1,
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
      Report(diagnostics, Severity::Error, DiagnosticCode::VerifyPointwiseDispatch, "", "", -1, -1,
             "dispatch branch 'OP_" + define +
                 "' names no operator of the op list; remove the branch or restore the operator");
      ++errors;
    }
  }
  for (const auto& entry : exemptions) {
    if (listed.count(entry.first) == 0) {
      Report(
          diagnostics, Severity::Error, DiagnosticCode::VerifyPointwiseDispatch, "", "", -1, -1,
          "exemption for '" + entry.first + "' names no operator of the op list; remove the entry");
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
    // The declared families' hook files (.glsl under modules/) are sources too: their defines are
    // visible to every includer, so they must join the known set like any shared .inc.
    if (extension != ".inc" && extension != ".frag" && extension != ".vert" &&
        extension != ".glsl") {
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

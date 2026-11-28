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

#include "compile/WebGPUDialect.h"
#include <algorithm>
#include <cctype>
#include <regex>
#include <string>
#include <vector>
#include "gpu/ShaderBindingLayout.h"

namespace tgfx {

#ifdef TGFX_SHADER_TOOL_HAS_TINT

// Rebuilds a string by applying a regex-driven replacer over successive matches, passing a
// mutable counter so replacers can allocate sequential binding numbers.
using MatchReplacer = std::string (*)(const std::smatch&, int&);

static std::string replaceAllMatches(const std::string& source, const std::regex& pattern,
                                     MatchReplacer replacer, int& counter) {
  std::smatch match;
  std::string::const_iterator searchStart(source.cbegin());
  std::string result;
  size_t lastPos = 0;
  while (std::regex_search(searchStart, source.cend(), match, pattern)) {
    auto matchPos = static_cast<size_t>(match.position(0));
    auto iterOffset = static_cast<size_t>(searchStart - source.cbegin());
    size_t matchStart = matchPos + iterOffset;
    result += source.substr(lastPos, matchStart - lastPos);
    result += replacer(match, counter);
    lastPos = matchStart + static_cast<size_t>(match.length(0));
    searchStart = match.suffix().first;
  }
  result += source.substr(lastPos);
  return result;
}

// Template shaders declare combined image samplers in the contract's texture set (the
// TGFX_TEXTURE_SET macro, expanded by the preprocessor before this pass runs). The binding
// expression may be a literal, a preprocessor macro, or a parenthesized macro expansion like
// "(4 + 1)", so it must tolerate one level of nested parentheses. The original numbers are
// ignored: bindings are reassigned in declaration order (the SamplerBinding packing), mirroring
// the runtime JIT path (WebGPUShaderModule::preprocessGLSL) and the WebGPURenderPipeline
// bind-group expansion.
static std::string WebGPUSamplerDeclPattern() {
  return R"(layout\s*\(\s*set\s*=\s*)" + std::to_string(TEXTURE_DESCRIPTOR_SET) +
         R"(\s*,\s*binding\s*=\s*(?:[^()]|\([^)]*\))*\s*\)\s*uniform\s+(?:sampler2D|CHAIN_LEAF_SAMPLER)\s+(\w+)\s*;)";
}

static std::vector<std::string> collectSamplerNames(const std::string& source) {
  static const std::regex samplerRegex(WebGPUSamplerDeclPattern());
  std::vector<std::string> names = {};
  std::smatch match;
  std::string::const_iterator searchStart(source.cbegin());
  while (std::regex_search(searchStart, source.cend(), match, samplerRegex)) {
    names.push_back(match[1].str());
    searchStart = match.suffix().first;
  }
  return names;
}

static std::string replaceSeparatedSampler(const std::smatch& match, int& counter) {
  const auto& name = match[1].str();
  auto set = std::to_string(TEXTURE_DESCRIPTOR_SET);
  auto textureDecl = "layout(set = " + set + ", binding = " + std::to_string(counter++) +
                     ") uniform texture2D " + name + ";";
  auto samplerDecl = "\nlayout(set = " + set + ", binding = " + std::to_string(counter++) +
                     ") uniform sampler " + name + "_Sampler;";
  return textureDecl + samplerDecl;
}

// Rewrites combined-sampler lookups to construct the combined sampler from its separated
// texture and sampler resources, which Vulkan GLSL supports directly.
static std::string rewriteTextureLookups(const std::string& source,
                                         const std::vector<std::string>& samplerNames) {
  auto result = source;
  for (const auto& name : samplerNames) {
    std::regex lookupRegex("texture\\(\\s*" + name + "\\s*,");
    result = std::regex_replace(result, lookupRegex,
                                "texture(sampler2D(" + name + ", " + name + "_Sampler),");
  }
  return result;
}

// WGSL has no combined image samplers, so functions taking one as a parameter need their
// signature split into separate texture and sampler parameters, and every call site needs the
// matching sampler argument appended at that parameter's position. Samplers may sit anywhere in
// the parameter list (the gradient LUT reader takes four after its scalar parameters), so the
// pass tracks positions rather than assuming the leading form.
struct SamplerParamFunction {
  std::string funcName;
  std::vector<size_t> samplerIndices = {};
  std::vector<std::string> samplerNames = {};
};

static std::vector<std::string> splitArguments(const std::string& arguments) {
  std::vector<std::string> result = {};
  std::string current = {};
  int depth = 0;
  for (const char character : arguments) {
    if (character == '(') {
      ++depth;
    } else if (character == ')') {
      --depth;
    }
    if (character == ',' && depth == 0) {
      result.push_back(current);
      current.clear();
    } else {
      current += character;
    }
  }
  if (!current.empty() || !result.empty()) {
    result.push_back(current);
  }
  return result;
}

static std::string lastWord(const std::string& text) {
  size_t end = text.size();
  while (end > 0 && !std::isalnum(static_cast<unsigned char>(text[end - 1])) &&
         text[end - 1] != '_') {
    --end;
  }
  size_t begin = end;
  while (begin > 0 && (std::isalnum(static_cast<unsigned char>(text[begin - 1])) != 0 ||
                       text[begin - 1] == '_')) {
    --begin;
  }
  return text.substr(begin, end - begin);
}

static std::vector<SamplerParamFunction> collectSamplerParamFunctions(const std::string& source) {
  static std::regex sigRegex(R"(\w+\s+(\w+)\s*\(([^)]*)\)\s*\{)");
  std::vector<SamplerParamFunction> functions = {};
  std::smatch match;
  std::string::const_iterator searchStart(source.cbegin());
  while (std::regex_search(searchStart, source.cend(), match, sigRegex)) {
    const auto arguments = splitArguments(match[2].str());
    SamplerParamFunction function = {};
    function.funcName = match[1].str();
    for (size_t index = 0; index < arguments.size(); ++index) {
      if (arguments[index].find("sampler2D") == std::string::npos &&
          arguments[index].find("CHAIN_LEAF_SAMPLER") == std::string::npos) {
        continue;
      }
      const std::string name = lastWord(arguments[index]);
      if (name.empty()) {
        continue;
      }
      function.samplerIndices.push_back(index);
      function.samplerNames.push_back(name);
    }
    if (!function.samplerIndices.empty()) {
      functions.push_back(std::move(function));
    }
    searchStart = match.suffix().first;
  }
  return functions;
}

static std::string replaceSplitSamplerParam(const std::smatch& match, int& /*counter*/) {
  const auto& name = match[1].str();
  return "texture2D " + name + ", sampler " + name + "_Sampler" + match[2].str();
}

static std::string splitSamplerParamSignatures(const std::string& source) {
  static std::regex paramRegex(R"((?:CHAIN_LEAF_SAMPLER|sampler2D)\s+(\w+)(\s*(?:,|\))))");
  int unused = 0;
  return replaceAllMatches(source, paramRegex, replaceSplitSamplerParam, unused);
}

// Expands the leading combined-sampler argument at call sites of sampler-param functions:
// "func(Tex, ..." becomes "func(Tex, Tex_Sampler, ...". The argument name must already refer to
// a known combined sampler (a top-level uniform or another sampler function parameter).
static bool isSamplerIndex(const SamplerParamFunction& function, size_t index) {
  for (const auto samplerIndex : function.samplerIndices) {
    if (samplerIndex == index) {
      return true;
    }
  }
  return false;
}

static bool isKnownName(const std::vector<std::string>& names, const std::string& name) {
  return std::find(names.begin(), names.end(), name) != names.end();
}

// Expands the combined-sampler argument at its own position in each call site: a known sampler
// name at a sampler parameter index gains the matching "_Sampler" argument right after it.
static std::string expandSamplerCallArgs(const std::string& source,
                                         const std::vector<SamplerParamFunction>& functions,
                                         const std::vector<std::string>& combinedNames) {
  auto result = source;
  for (const auto& function : functions) {
    const std::string call = function.funcName + "(";
    size_t offset = 0;
    while ((offset = result.find(call, offset)) != std::string::npos) {
      const size_t open = offset + call.size() - 1;
      int depth = 0;
      size_t close = open;
      for (; close < result.size(); ++close) {
        if (result[close] == '(') {
          ++depth;
        } else if (result[close] == ')') {
          --depth;
          if (depth == 0) {
            break;
          }
        }
      }
      if (close >= result.size()) {
        break;
      }
      const auto arguments = splitArguments(result.substr(open + 1, close - open - 1));
      std::string rebuilt;
      for (size_t index = 0; index < arguments.size(); ++index) {
        if (index > 0) {
          rebuilt += ", ";
        }
        const std::string& argument = arguments[index];
        const size_t first = argument.find_first_not_of(" \t\r\n");
        const size_t last = argument.find_last_not_of(" \t\r\n");
        const std::string trimmed =
            first == std::string::npos ? "" : argument.substr(first, last - first + 1);
        if (isSamplerIndex(function, index) && isKnownName(combinedNames, trimmed)) {
          rebuilt += trimmed + ", " + trimmed + "_Sampler";
        } else {
          rebuilt += argument;
        }
      }
      result.replace(open + 1, close - open - 1, rebuilt);
      offset = open + 1 + rebuilt.size() + 1;
    }
  }
  return result;
}

static std::string preprocessWebGPU(const std::string& source) {
  auto samplerNames = collectSamplerNames(source);
  auto samplerParamFunctions = collectSamplerParamFunctions(source);
  auto result = splitSamplerParamSignatures(source);
  // Function bodies index the combined sampler through the parameter name, so parameter names
  // join the uniform names for texture() rewriting.
  auto lookupNames = samplerNames;
  for (const auto& function : samplerParamFunctions) {
    lookupNames.insert(lookupNames.end(), function.samplerNames.begin(),
                       function.samplerNames.end());
  }
  static const std::regex samplerDeclRegex(WebGPUSamplerDeclPattern());
  int binding = WEBGPU_TEXTURE_BINDING_START;
  result = replaceAllMatches(result, samplerDeclRegex, replaceSeparatedSampler, binding);
  result = rewriteTextureLookups(result, lookupNames);
  return expandSamplerCallArgs(result, samplerParamFunctions, lookupNames);
}

std::string RewriteSamplerLayoutForWGSL(const std::string& source) {
  return preprocessWebGPU(source);
}

#endif  // TGFX_SHADER_TOOL_HAS_TINT

}  // namespace tgfx

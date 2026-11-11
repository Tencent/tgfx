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
#include <regex>
#include <string>
#include <vector>
#include "gpu/shaders/contract/ShaderBundleContract.h"

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
// matching sampler argument appended. Only the leading-parameter form is supported; a sampler
// in any other parameter position fails to match, surfaces as a shaderc error, and must be
// handled by extending this pass.
struct SamplerParamFunction {
  std::string funcName;
  std::string paramName;
};

static std::vector<SamplerParamFunction> collectSamplerParamFunctions(const std::string& source) {
  static std::regex sigRegex(R"(\w+\s+(\w+)\s*\(\s*(?:CHAIN_LEAF_SAMPLER|sampler2D)\s+(\w+)\s*,)");
  std::vector<SamplerParamFunction> functions = {};
  std::smatch match;
  std::string::const_iterator searchStart(source.cbegin());
  while (std::regex_search(searchStart, source.cend(), match, sigRegex)) {
    functions.push_back({match[1].str(), match[2].str()});
    searchStart = match.suffix().first;
  }
  return functions;
}

static std::string replaceSplitSamplerParam(const std::smatch& match, int& /*counter*/) {
  const auto& name = match[1].str();
  return "texture2D " + name + ", sampler " + name + "_Sampler,";
}

static std::string splitSamplerParamSignatures(const std::string& source) {
  static std::regex paramRegex(R"((?:CHAIN_LEAF_SAMPLER|sampler2D)\s+(\w+)\s*,)");
  int unused = 0;
  return replaceAllMatches(source, paramRegex, replaceSplitSamplerParam, unused);
}

// Expands the leading combined-sampler argument at call sites of sampler-param functions:
// "func(Tex, ..." becomes "func(Tex, Tex_Sampler, ...". The argument name must already refer to
// a known combined sampler (a top-level uniform or another sampler function parameter).
static std::string expandSamplerCallArgs(const std::string& source,
                                         const std::vector<SamplerParamFunction>& functions,
                                         const std::vector<std::string>& combinedNames) {
  auto result = source;
  for (const auto& function : functions) {
    std::regex callRegex("(" + function.funcName + R"(\s*\(\s*)(\w+))");
    std::smatch match;
    std::string::const_iterator searchStart(result.cbegin());
    std::string expanded;
    size_t lastPos = 0;
    const std::string& subject = result;
    while (std::regex_search(searchStart, subject.cend(), match, callRegex)) {
      auto matchPos = static_cast<size_t>(match.position(0));
      auto iterOffset = static_cast<size_t>(searchStart - subject.cbegin());
      size_t matchStart = matchPos + iterOffset;
      expanded += subject.substr(lastPos, matchStart - lastPos);
      auto argName = match[2].str();
      if (std::find(combinedNames.begin(), combinedNames.end(), argName) != combinedNames.end()) {
        expanded += match[1].str() + argName + ", " + argName + "_Sampler";
      } else {
        expanded += match.str();
      }
      lastPos = matchStart + static_cast<size_t>(match.length(0));
      searchStart = match.suffix().first;
    }
    expanded += subject.substr(lastPos);
    result = expanded;
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
    lookupNames.push_back(function.paramName);
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

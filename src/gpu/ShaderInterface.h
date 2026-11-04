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

#pragma once

#include <regex>
#include <string>
#include <unordered_map>
#include "tgfx/gpu/ShaderModule.h"
#include "tgfx/gpu/ShaderStage.h"

namespace tgfx {

/// Extracts the varying declarations of a shader stage: the `out` declarations for the vertex
/// stage and the `in` declarations for the fragment stage. Each varying is keyed by name and maps
/// to the number of locations it occupies (1 for scalars/vectors, the array length for array-typed
/// varyings). This set defines the cross-stage varying interface; the vertex and fragment stages
/// must declare the exact same interface or the pipeline is rejected.
inline std::unordered_map<std::string, int> ExtractVaryingDecls(const std::string& glslCode,
                                                                ShaderStage stage) {
  static std::regex inVarRegex(
      R"((flat\s+|noperspective\s+)?in\s+(highp\s+|mediump\s+|lowp\s+)?(\w+)\s+(\w+)(\s*\[\s*(\d+)\s*\])?\s*;)");
  static std::regex outVarRegex(
      R"((flat\s+|noperspective\s+)?out\s+(highp\s+|mediump\s+|lowp\s+)?(\w+)\s+(\w+)(\s*\[\s*(\d+)\s*\])?\s*;)");
  const std::regex& pattern = (stage == ShaderStage::Vertex) ? outVarRegex : inVarRegex;
  std::unordered_map<std::string, int> decls;
  std::smatch match;
  std::string::const_iterator searchStart(glslCode.cbegin());
  while (std::regex_search(searchStart, glslCode.cend(), match, pattern)) {
    int step = match[6].matched ? std::stoi(match[6].str()) : 1;
    decls[match[4].str()] = step;
    searchStart = match.suffix().first;
  }
  return decls;
}

/// Checks whether the vertex and fragment stages declare an identical varying interface. Returns
/// true when the name sets match and every varying has the same array length on both sides;
/// otherwise writes a description of the first mismatch into `mismatchDescription` and returns
/// false.
inline bool VaryingInterfacesMatch(const std::unordered_map<std::string, int>& vertexDecls,
                                   const std::unordered_map<std::string, int>& fragmentDecls,
                                   std::string& mismatchDescription) {
  for (const auto& [name, locations] : fragmentDecls) {
    auto it = vertexDecls.find(name);
    if (it == vertexDecls.end()) {
      mismatchDescription = "fragment input '" + name + "' has no matching vertex output";
      return false;
    }
    if (it->second != locations) {
      mismatchDescription = "varying '" + name + "' has an inconsistent array length";
      return false;
    }
  }
  for (const auto& entry : vertexDecls) {
    if (fragmentDecls.find(entry.first) == fragmentDecls.end()) {
      mismatchDescription = "vertex output '" + entry.first + "' is not consumed by the fragment";
      return false;
    }
  }
  return true;
}

/// Extracts the varying declarations a module participates in. Only GLSL text is parseable here;
/// the native formats (MSL, WGSL, HLSL, SPIRV, METALLIB, DXBC) carry no GLSL interface for this
/// scan, so they report an empty set and the cross-stage check is skipped — their compilers and
/// the backend validate the interface instead.
inline std::unordered_map<std::string, int> ExtractModuleVaryingDecls(
    const ShaderModuleDescriptor& descriptor) {
  if (descriptor.format != ShaderCodeFormat::GLSL) {
    return {};
  }
  return ExtractVaryingDecls(descriptor.code, descriptor.stage);
}

}  // namespace tgfx

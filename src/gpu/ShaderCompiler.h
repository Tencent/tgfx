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

#include <algorithm>
#include <cstdint>
#include <map>
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>
#include "UniformData.h"
#include "core/utils/Log.h"
#include "tgfx/gpu/ShaderStage.h"

namespace shaderc {
class Compiler;
}

namespace tgfx {

struct ShaderUniformBinding {
  std::string name;
  uint32_t descriptorSet = 0;
  uint32_t binding = 0;
};

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

// Shared GLSL-to-Vulkan preprocessing passes used by both the SPIR-V backends (ShaderCompiler.cpp)
// and the WebGPU backend. They are inline so the WebGPU backend can reuse them without linking
// ShaderCompiler.cpp, which is excluded from WebGPU-only builds.
namespace detail {

// Replaces every match of `pattern` in `source`, advancing `counter` by each match's location
// footprint and letting `replacer` format the replacement declaration.
template <typename Replacer>
inline std::string ReplaceAllMatches(const std::string& source, const std::regex& pattern,
                                     Replacer replacer, int& counter) {
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

inline int UniformDescriptorSet(ShaderStage stage) {
  return stage == ShaderStage::Vertex ? VERTEX_UBO_DESCRIPTOR_SET : FRAGMENT_UBO_DESCRIPTOR_SET;
}

// Assigns fixed local binding points for internal UBOs. Vertex and fragment UBOs live in separate
// descriptor sets, so their numeric bindings are physical stage-local slots rather than public
// BindingEntry.binding values.
inline std::string AssignInternalUBOBindings(const std::string& source) {
  static std::regex vertexUboRegex(R"(layout\s*\(\s*std140\s*\)\s*uniform\s+VertexUniformBlock)");
  auto result =
      std::regex_replace(source, vertexUboRegex,
                         "layout(std140, set=" + std::to_string(VERTEX_UBO_DESCRIPTOR_SET) +
                             ", binding=0) uniform VertexUniformBlock");
  static std::regex fragmentUboRegex(
      R"(layout\s*\(\s*std140\s*\)\s*uniform\s+FragmentUniformBlock)");
  return std::regex_replace(result, fragmentUboRegex,
                            "layout(std140, set=" + std::to_string(FRAGMENT_UBO_DESCRIPTOR_SET) +
                                ", binding=0) uniform FragmentUniformBlock");
}

inline int NextUniformBinding(const std::string& source, int descriptorSet) {
  static std::regex boundUboRegex(
      R"(layout\s*\(\s*std140\s*,\s*set\s*=\s*(\d+)\s*,\s*binding\s*=\s*(\d+)\s*\)\s*uniform)");
  int nextBinding = 0;
  std::smatch match;
  std::string::const_iterator searchStart(source.cbegin());
  while (std::regex_search(searchStart, source.cend(), match, boundUboRegex)) {
    if (std::stoi(match[1].str()) == descriptorSet) {
      nextBinding = std::max(nextBinding, std::stoi(match[2].str()) + 1);
    }
    searchStart = match.suffix().first;
  }
  return nextBinding;
}

struct CustomUBOBindingReplacer {
  int descriptorSet = 0;
  std::string operator()(const std::smatch& match, int& counter) const {
    return "layout(std140, set=" + std::to_string(descriptorSet) +
           ", binding=" + std::to_string(counter++) + ") uniform " + match[1].str();
  }
};

// Adds physical bindings to remaining uniform blocks sequentially within the shader stage's
// descriptor set. The public BindingEntry.binding is resolved by name at pipeline creation time.
inline std::string AssignCustomUBOBindings(const std::string& source, ShaderStage stage) {
  static std::regex uboRegex(R"(layout\s*\(\s*std140\s*\)\s*uniform\s+(\w+))");
  int binding = NextUniformBinding(source, UniformDescriptorSet(stage));
  return ReplaceAllMatches(source, uboRegex, CustomUBOBindingReplacer{UniformDescriptorSet(stage)},
                           binding);
}

// Extracts uniform-block names and physical descriptor locations from preprocessed GLSL.
inline std::vector<ShaderUniformBinding> CollectUniformBindings(
    const std::string& preprocessedGLSL) {
  static std::regex uboRegex(
      R"(layout\s*\(\s*std140\s*,\s*set\s*=\s*(\d+)\s*,\s*binding\s*=\s*(\d+)\s*\)\s*uniform\s+(\w+))");
  std::vector<ShaderUniformBinding> bindings;
  std::smatch match;
  std::string::const_iterator searchStart(preprocessedGLSL.cbegin());
  while (std::regex_search(searchStart, preprocessedGLSL.cend(), match, uboRegex)) {
    bindings.push_back({match[3].str(), static_cast<uint32_t>(std::stoul(match[1].str())),
                        static_cast<uint32_t>(std::stoul(match[2].str()))});
    searchStart = match.suffix().first;
  }
  return bindings;
}

// Builds the uniform-block name -> physical binding map from preprocessed GLSL, ordered by name
// for deterministic slot resolution. The physical binding is stage-local: it lives in a per-stage
// descriptor set, so the name uniquely identifies the block within the stage.
inline std::map<std::string, unsigned> CollectUniformSlots(const std::string& preprocessedGLSL) {
  std::map<std::string, unsigned> slots;
  for (const auto& uniform : CollectUniformBindings(preprocessedGLSL)) {
    slots[uniform.name] = uniform.binding;
  }
  return slots;
}

inline std::string ReplaceInputLocation(const std::smatch& match, int& counter) {
  std::string interpStr = match[1].matched ? match[1].str() : "";
  std::string precisionStr = match[2].matched ? match[2].str() : "";
  // match[5] is the whole array suffix like "[5]" (preserved verbatim in the emitted decl);
  // match[6] is just the size digits, used to advance the location counter.
  std::string arraySuffix = match[5].matched ? match[5].str() : "";
  int step = match[6].matched ? std::stoi(match[6].str()) : 1;
  std::string decl = "layout(location=" + std::to_string(counter) + ") " + interpStr + "in " +
                     precisionStr + match[3].str() + " " + match[4].str() + arraySuffix + ";";
  counter += step;
  return decl;
}

inline std::string ReplaceOutputLocation(const std::smatch& match, int& counter) {
  std::string interpStr = match[1].matched ? match[1].str() : "";
  std::string precisionStr = match[2].matched ? match[2].str() : "";
  std::string arraySuffix = match[5].matched ? match[5].str() : "";
  int step = match[6].matched ? std::stoi(match[6].str()) : 1;
  std::string decl = "layout(location=" + std::to_string(counter) + ") " + interpStr + "out " +
                     precisionStr + match[3].str() + " " + match[4].str() + arraySuffix + ";";
  counter += step;
  return decl;
}

// Rewrites interface-variable declarations so their `layout(location=N)` numbers are assigned by
// looking up each variable's name in `nameToLocation`, instead of in source order. The map is
// built from the same regex scan this pass consumes, so every matched name is guaranteed to be
// present; the assertion below documents that contract.
inline std::string ReplaceWithNameKeyedLocations(
    const std::string& source, const std::regex& pattern,
    const std::unordered_map<std::string, int>& nameToLocation, bool isInput) {
  std::smatch match;
  std::string::const_iterator searchStart(source.cbegin());
  std::string result;
  size_t lastPos = 0;
  while (std::regex_search(searchStart, source.cend(), match, pattern)) {
    auto matchPos = static_cast<size_t>(match.position(0));
    auto iterOffset = static_cast<size_t>(searchStart - source.cbegin());
    size_t matchStart = matchPos + iterOffset;
    result += source.substr(lastPos, matchStart - lastPos);

    std::string interpStr = match[1].matched ? match[1].str() : "";
    std::string precisionStr = match[2].matched ? match[2].str() : "";
    std::string arraySuffix = match[5].matched ? match[5].str() : "";
    const std::string& name = match[4].str();
    auto it = nameToLocation.find(name);
    DEBUG_ASSERT(it != nameToLocation.end());
    int location = it->second;
    result += "layout(location=" + std::to_string(location) + ") " + interpStr +
              (isInput ? "in " : "out ") + precisionStr + match[3].str() + " " + name +
              arraySuffix + ";";

    lastPos = matchStart + static_cast<size_t>(match.length(0));
    searchStart = match.suffix().first;
  }
  result += source.substr(lastPos);
  return result;
}

struct NameSortedEntry {
  std::string name;
  int step;
};

inline bool CompareEntryNames(const NameSortedEntry& a, const NameSortedEntry& b) {
  return a.name < b.name;
}

// Builds a `name -> location` map from the interface declarations matched by `pattern`. Names are
// collected in source order, then sorted lexicographically before locations are assigned. Array-
// typed varyings occupy `size` consecutive locations, so the counter is advanced by the array
// length to match SPIR-V semantics for per-vertex block members.
inline std::unordered_map<std::string, int> BuildNameSortedLocationMap(const std::string& source,
                                                                       const std::regex& pattern) {
  std::vector<NameSortedEntry> entries;
  std::smatch match;
  std::string::const_iterator searchStart(source.cbegin());
  while (std::regex_search(searchStart, source.cend(), match, pattern)) {
    int step = match[6].matched ? std::stoi(match[6].str()) : 1;
    entries.push_back({match[4].str(), step});
    searchStart = match.suffix().first;
  }
  std::sort(entries.begin(), entries.end(), CompareEntryNames);
  std::unordered_map<std::string, int> nameToLocation;
  int location = 0;
  for (const auto& entry : entries) {
    nameToLocation[entry.name] = location;
    location += entry.step;
  }
  return nameToLocation;
}

inline std::string UpgradeGLSLVersion(const std::string& source) {
  static std::regex versionRegex(R"(#version\s+\d+(\s+es)?)");
  return std::regex_replace(source, versionRegex, "#version 450");
}

// Removes precision qualifiers that are not supported in desktop GLSL 450.
inline std::string RemovePrecisionDeclarations(const std::string& source) {
  static std::regex precisionDeclRegex(R"(precision\s+(highp|mediump|lowp)\s+\w+\s*;)");
  return std::regex_replace(source, precisionDeclRegex, "");
}

// Adds location qualifiers to 'in' variables. Vertex stage inputs are vertex attributes whose
// locations must match the attribute order declared in `RenderPipelineDescriptor::vertex
// .bufferLayouts` (a CPU-side contract shared by Metal / Vulkan / D3D12), so they keep the source-
// declaration order. Fragment stage inputs are varyings paired with the vertex stage's outputs;
// assigning their locations by sorted name makes the pairing independent of source order.
inline std::string AssignInputLocationQualifiers(const std::string& source, ShaderStage stage) {
  static std::regex inVarRegex(
      R"((flat\s+|noperspective\s+)?in\s+(highp\s+|mediump\s+|lowp\s+)?(\w+)\s+(\w+)(\s*\[\s*(\d+)\s*\])?\s*;)");
  if (stage == ShaderStage::Vertex) {
    int location = 0;
    return ReplaceAllMatches(source, inVarRegex, ReplaceInputLocation, location);
  }
  auto nameToLocation = BuildNameSortedLocationMap(source, inVarRegex);
  return ReplaceWithNameKeyedLocations(source, inVarRegex, nameToLocation, /*isInput=*/true);
}

// Adds location qualifiers to 'out' variables. Vertex stage outputs are varyings feeding the
// fragment stage; assigning their locations by sorted name (the same rule used for fragment
// inputs) makes the vertex→fragment pairing independent of source order. Fragment stage outputs
// are colour attachments whose location index maps directly to the colour attachment slot, so they
// keep the source-declaration order.
inline std::string AssignOutputLocationQualifiers(const std::string& source, ShaderStage stage) {
  static std::regex outVarRegex(
      R"((flat\s+|noperspective\s+)?out\s+(highp\s+|mediump\s+|lowp\s+)?(\w+)\s+(\w+)(\s*\[\s*(\d+)\s*\])?\s*;)");
  if (stage == ShaderStage::Fragment) {
    int location = 0;
    return ReplaceAllMatches(source, outVarRegex, ReplaceOutputLocation, location);
  }
  auto nameToLocation = BuildNameSortedLocationMap(source, outVarRegex);
  return ReplaceWithNameKeyedLocations(source, outVarRegex, nameToLocation, /*isInput=*/false);
}

}  // namespace detail

/// Preprocesses OpenGL-style GLSL source code to Vulkan-compatible GLSL 450 with explicit
/// binding/location qualifiers. This includes upgrading the #version directive, assigning UBO and
/// sampler bindings, adding input/output location qualifiers, and removing precision declarations.
///
/// `stage` selects the semantics for interface variable location assignment so that varying
/// declarations line up across the vertex/fragment boundary regardless of source order:
///   - Vertex stage: `in` (vertex attributes) keep their source-declaration order — the assigned
///     locations must match the attribute order in `RenderPipelineDescriptor::vertex.bufferLayouts`,
///     which is the CPU-side contract every backend (Metal, Vulkan, D3D12) relies on. `out`
///     (varying) declarations are collected and sorted by name before receiving locations, so the
///     fragment side can pair them up without knowing the vertex side's source order.
///   - Fragment stage: `in` (varying) declarations are collected and sorted by name using the
///     same rule as the vertex `out` side, guaranteeing matching locations across stages. `out`
///     (colour attachments) keep their source-declaration order to preserve the mapping to the
///     colour attachment index.
///
/// Name-based cross-stage pairing requires both stages to declare the exact same set of varying
/// names with matching array lengths. A varying declared on only one side (an unused vertex output
/// or an unmatched fragment input) desyncs the two stages, so such pipelines are rejected at
/// creation time via VaryingInterfacesMatch().
std::string PreprocessGLSL(const std::string& glslCode, ShaderStage stage);

/// Compiles preprocessed GLSL 450 source to SPIR-V binary using shaderc. Returns an empty vector
/// on failure.
///
/// `preserveInterfaceVariables` controls the optimisation level:
///   - false (default): runs `shaderc_optimization_level_performance`, which is what Vulkan and
///     Metal want — both bind interface variables by name/location, so dead-stripping unused
///     fragment inputs is harmless and yields better generated code.
///   - true: runs `shaderc_optimization_level_zero` so every declared vertex output / fragment
///     input survives. D3D12 needs this because the SPIR-V → HLSL pass turns SPIR-V locations
///     into TEXCOORDn semantics; if the optimiser drops a fragment input, the resulting HLSL
///     mismatches the vertex shader's output signature and PSO creation fails.
std::vector<uint32_t> CompileGLSLToSPIRV(const shaderc::Compiler* compiler,
                                         const std::string& vulkanGLSL, ShaderStage stage,
                                         bool preserveInterfaceVariables = false);

}  // namespace tgfx

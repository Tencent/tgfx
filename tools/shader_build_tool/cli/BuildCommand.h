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

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "gpu/SLType.h"

namespace tgfx {

class StageCompiler;

/// The SLType of one GLSL type spelling, or Void outside the supported set.
SLType SLTypeFromName(const std::string& glslType);

/// The parsed command line of a build run.
struct BuildOptions {
  std::string shaderDir;
  /// The library bundle the tool writes (--library-bundle). The tool has no other product: the
  /// variant bundles retired with the ahead-of-time route.
  std::string libraryBundlePath;
  /// The languages the library bundle carries (--library-languages, comma separated). "glsl" is
  /// always written; "msl" adds the Metal section, which needs the compiler to translate.
  std::string libraryLanguages = "glsl";
  // The platform the Metal section's compile flags target (see SetMetalTarget).
  std::string metalSdk = "macosx";
  std::string metalMinOS;
  /// What a d3d12 section stores: dxbc (bytecode compiled here, the default) or hlsl (source the
  /// backend compiles). Requesting dxbc on a host without D3DCompiler fails the build that needs
  /// the bytecode, not every unrelated build.
  std::string d3d12ShaderFormat = "dxbc";
};

/// The role one operator parameter plays when the generated glue emits a call.
enum class OperatorParamRole : uint8_t {
  /// An IR value the caller passes to Emit (a `/*@in*/` parameter).
  In,
  /// A uniform member of the fragment (or vertex) block; the typed Uniforms struct owns its value.
  Uniform,
  /// A fixed-length uniform array (`T name[N]`).
  UniformArray,
  /// A texture sampler the caller registers before Emit.
  Sampler,
};

/// One parameter of a declared operator, derived from the .inc signature.
struct OperatorParamInfo {
  /// The GLSL base type spelled in the signature ("float", "vec4", "mat3", "sampler2D", ...).
  std::string type;
  std::string name;
  /// The array length, zero when the parameter is not an array.
  uint32_t arrayCount = 0;
  OperatorParamRole role = OperatorParamRole::Uniform;
  /// True only for an `/*@in*/ int` parameter: the generated Emit takes a literal, the printers
  /// fold it into the text, and the processor key must encode it.
  bool intLiteral = false;
};

/// One `//@op`-annotated catalog function: the operator the runtime may call directly.
struct OperatorSignature {
  /// The operator name from `//@op <Name>`, Pascal-cased ("Luma").
  std::string opName;
  /// The GLSL function name the printers spell at the call site.
  std::string functionName;
  /// "vertex" or "fragment".
  std::string stage;
  /// The GLSL return type of the function ("vec4"), mapped to SLType at generation.
  std::string returnType;
  /// The module sources in compile order (the dependency closure, callees before callers), paths
  /// relative to the shader directory — the runtime manifest's includes.
  std::vector<std::string> includes;
  std::vector<OperatorParamInfo> params;
};

/// Collects the operator signatures of every `//@op`-annotated function under <shader-dir>/fn
/// (and the geometry coverage files). The parameter roles derive from the signatures; any
/// ambiguity (a sampler marked /*@in*/, an in-array, an unknown type, a duplicate op name) fails
/// with an error naming the function.
bool CollectOperatorSignatures(const std::string& shaderDir,
                               std::vector<OperatorSignature>* signatures, std::string* error);

/// Generates the typed C++ glue (GeneratedOperators.h/.cpp) from the collected signatures: the
/// operator id enum sorted by name, the description table, and one <Op>Uniforms struct per
/// operator. The output is deterministic: unchanged input rewrites identical bytes.
bool WriteGeneratedOperators(const std::vector<OperatorSignature>& signatures,
                             const std::string& outHeader, const std::string& outSource,
                             const std::string& outIdsHeader, std::string* error);

/// The usage text, one entry per option ParseArgs accepts. Returned rather than printed so a test
/// can assert the documented options and the parser agree (see test/shader_tool/CliTest.cpp).
std::string UsageText();

/// Writes UsageText() to stderr.
void PrintUsage();

/// Parses argv into options. Prints the usage and returns false on an unknown option.
bool ParseArgs(int argc, char** argv, BuildOptions* options);

/**
 * Scans <shader-dir>/fn for annotated functions (`//@function name [opPrefix]`), builds the call
 * graph from the function bodies (duplicate names, dangling requirements, cycles) and writes the
 * single shader library bundle. The GLSL section is always written; a non-null compiler additionally
 * translates every function to MSL and appends the MSL section, so a Metal build assembles its
 * shaders from the same per-function pieces. withSpirv appends the SPIR-V section: one linkable
 * module per operator, the payload the Vulkan stitching route links against its printed main.
 */
bool WriteShaderLibraryBundle(const std::string& shaderDir, const std::string& outPath,
                              StageCompiler* compiler, bool withWgsl, bool withSpirv,
                              std::string* error);

}  // namespace tgfx

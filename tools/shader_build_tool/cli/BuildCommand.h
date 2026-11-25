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

#include <optional>
#include <string>
#include <vector>
#include "gpu/shaders/contract/ShaderBundleContract.h"

namespace tgfx {

class StageCompiler;

/// The parsed command line of a build run.
struct BuildOptions {
  std::string shaderDir;
  std::string outDir;
  /// When set, the tool only scans the fn/ directory and writes the shader library bundle the
  /// runtime embeds (no variant bundles are built).
  std::string libraryBundlePath;
  /// The languages the library bundle carries (--library-languages, comma separated). "glsl" is
  /// always written; "msl" adds the Metal section, which needs the compiler to translate.
  std::string libraryLanguages = "glsl";
  std::vector<ShaderProfile> backends;
  bool compress = false;
  // Persistent stage cache. Defaults to <out-dir>/stage_cache; --no-cache keeps it memory-only.
  std::string cacheDir;
  bool noCache = false;
  /// The stage cache's size cap (--cache-max-mb, 2GiB by default): the open-time eviction brings
  /// the current identity's entries down to 80% of it.
  uint64_t cacheMaxBytes = 2048ull << 20;
  // The platform the Metal bundle is compiled for (see SetMetalTarget).
  std::string metalSdk = "macosx";
  /// What a d3d12 bundle stores: dxbc (bytecode compiled here, the default) or hlsl (source the
  /// backend compiles). Requesting dxbc on a host without D3DCompiler fails the build rather than
  /// quietly producing the other form.
  std::string d3d12ShaderFormat = "dxbc";
  std::string metalMinOS;
  /// Where the stamp manifest lands (the build's bundle_stamp): the tool's identity plus every
  /// written bundle's digest, so a later --verify can prove the artifacts on disk are the ones
  /// this tool produced.
  std::string stampPath;
  /// Verify a stamp manifest and exit: every recorded bundle must exist, match its digest, and
  /// have been produced by the running tool.
  std::string verifyPath;
};

/// One bundle recorded in the stamp manifest.
struct StampFileEntry {
  /// The backend tag the bundle carries (its filename's middle segment).
  std::string backend;
  /// The bundle's path on disk.
  std::string path;
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
/// operator. The Emit half of the glue lands when the operator id enum merges into the runtime
/// manifest. The output is deterministic: unchanged input rewrites identical bytes.
bool WriteGeneratedOperators(const std::vector<OperatorSignature>& signatures,
                             const std::string& outHeader, const std::string& outSource,
                             std::string* error);

/// Writes the stamp manifest: the generating tool's identity, a schema line, and one row per
/// bundle with its SHA-256 and size. Atomic (a temp file renamed into place), so a failed write
/// never leaves a half stamp behind.
bool WriteStampManifest(const std::string& stampPath, const std::string& toolIdentity,
                        const std::vector<StampFileEntry>& entries, std::string* error);

/// Reads a stamp manifest and re-checks everything it records: the bundles must exist with their
/// recorded sizes and digests, and the manifest must have been written by the running tool. Every
/// failure message ends with the same fix (delete the stamp and rebuild), so a stale or tampered
/// artifact is one command away from regeneration.
bool VerifyStampManifest(const std::string& stampPath, const std::string& toolIdentity,
                         std::string* error);

/// The usage text, one entry per option ParseArgs accepts. Returned rather than printed so a test
/// can assert the documented options and the parser agree (see test/shader_tool/CliTest.cpp).
std::string UsageText();

/// Writes UsageText() to stderr.
void PrintUsage();

/// Parses argv into options. Prints the usage and returns false on an unknown option or an
/// unknown backend tag.
bool ParseArgs(int argc, char** argv, BuildOptions* options);

/**
 * Scans <shader-dir>/fn for annotated functions (`//@function name [opPrefix]`), builds the call
 * graph from the function bodies (duplicate names, dangling requirements, cycles) and writes the
 * single shader library bundle. The GLSL section is always written; a non-null compiler additionally
 * translates every function to MSL and appends the MSL section, so a Metal build assembles its
 * shaders from the same per-function pieces. The chain operator registry derived from the same scan
 * lands beside the bundle.
 */
bool WriteShaderLibraryBundle(const std::string& shaderDir, const std::string& outPath,
                              StageCompiler* compiler, std::string* error);

/// Runs the build: loads the sources, plans and compiles every family's variants through the
/// emitters, runs the contract rules, and writes the bundles and the reports. Returns the process
/// exit code.
int RunBuildCommand(const BuildOptions& options);

}  // namespace tgfx

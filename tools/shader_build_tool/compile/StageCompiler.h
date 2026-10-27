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
#include <map>
#include <string>
#include <tuple>
#include <vector>
#include "ShaderCompiler.h"
#include "compile/ReflectionExtractor.h"
#include "compile/StageCache.h"

namespace tgfx {

// The compile sublayer of the tool. StageCompiler owns every cache of compiled stages for one
// build and is the only place that calls the compiler primitives below, so a stage is compiled
// exactly once per run however many backends consume it:
//
//                  cli / emit
//                      |
//              StageCompiler (this layer)
//            /       |        \            \
//   ShaderCompiler  StageCache  ReflectionExtractor  (the vendors sit below ShaderCompiler)
//
// The persistent cache and the reflection extractor are injected, never reached through global
// state, so every part of the layer can be tested on its own.

/// A compiled SPIR-V stage: spirv holds the module words on success, error names the failure.
struct SpirvStage {
  bool success = false;
  std::vector<uint32_t> spirv;
  std::string error;
};

/// A translated text stage (WGSL): text holds the translation on success, error names the
/// failure.
struct TextStage {
  bool success = false;
  std::string text;
  std::string error;
};

// The compiler primitives, defined in ShaderCompiler.cpp and private to this layer: nothing
// outside it can compile anything, which is what keeps the caching single-sourced. Each returns
// an empty result on failure and names the failed step through *error.

/// Compiles GLSL source (already with #defines prepended) to SPIR-V using shaderc. When
/// optimize is true, applies performance-level optimization to reduce SPIR-V size.
std::vector<uint32_t> CompileGLSL(const std::string& source, ShaderStageType stage,
                                  const std::string& shaderName, uint32_t variantIndex,
                                  bool optimize, std::string* error);

/// Translates SPIR-V binary to Metal Shading Language via spirv-cross.
std::string TranslateToMSL(const std::vector<uint32_t>& spirv, ShaderStageType stage,
                           std::string* error);

/// Converts #define-expanded template GLSL to WGSL: preprocess into Vulkan GLSL with
/// separated texture/sampler bindings, compile to SPIR-V via shaderc, then translate to
/// WGSL via tint. Binding contract (must match WebGPURenderPipeline and the runtime JIT
/// path in WebGPUShaderModule): group 0; binding 0/1 = vertex/fragment uniform blocks;
/// per sampler i (declaration order): texture binding = 2+2i, sampler binding = 3+2i.
std::string CompileGLSLToWGSL(const std::string& source, ShaderStageType stage,
                              const std::string& shaderName, uint32_t variantIndex,
                              std::string* error);

/// Compiles MSL source text to Metal library binary (.metallib) using xcrun metal/metallib, run
/// without a shell on files in a scratch directory private to this process. Returns an empty
/// vector on failure, with *error naming the failed step, its exit status and the compiler's
/// stderr. Always fails on Windows, which has no Metal toolchain.
std::vector<uint8_t> CompileMSLToMetallib(const std::string& mslSource, ShaderStageType stage,
                                          std::string* error);

/// Owns the reuse of compiled stages of one build. Every expensive compilation (GLSL to SPIR-V,
/// the optimized Vulkan SPIR-V, MSL to metallib, GLSL to WGSL) is looked up by a key built from
/// everything that feeds it: the exact text the compiler receives, the stage, the compiler
/// options, and the identity of this tool (see StageCacheKey). The memory layer removes the
/// repeats within one run, such as a vertex permutation shared by many fragment variants; the
/// persistent layer (the injected StageCache) lets a later run skip every stage whose inputs did
/// not change. Failures are only remembered for this run.
class StageCompiler {
 public:
  /// persistentCache may be unopened (memory-only, the --no-cache mode) but must outlive the
  /// compiler.
  explicit StageCompiler(StageCache* persistentCache);

  /// Compiles one GLSL stage to SPIR-V, reusing a previous compilation of the same inputs.
  const SpirvStage& compileSpirv(const std::string& source, ShaderStageType stage,
                                 const std::string& shaderName, uint32_t variantIndex,
                                 bool optimize);

  /// Compiles MSL text to a metallib, reusing a previous compilation. Returns an empty vector
  /// on failure, with *error naming the failed step.
  std::vector<uint8_t> compileMetallib(const std::string& msl, ShaderStageType stage,
                                       std::string* error);

  /// Translates GLSL to WGSL, reusing a previous translation of the same inputs.
  const TextStage& compileWgsl(const std::string& source, ShaderStageType stage,
                               const std::string& shaderName, uint32_t variantIndex);

  /// Extracts the reflection of one variant, shared across backend passes like the compiles
  /// above: the same (family, vert, frag) reflects the same two SPIR-V modules every time.
  const ReflectionResult& extractReflection(const std::string& shaderName, uint32_t vertIndex,
                                            uint32_t fragIndex,
                                            const std::vector<uint32_t>& vertSpirv,
                                            const std::vector<uint32_t>& fragSpirv,
                                            Diagnostics* diagnostics);

 private:
  struct BlobStage {
    bool success = false;
    std::vector<uint8_t> bytes;
    std::string error;
  };

  StageCache* persistentCache = nullptr;
  std::map<std::string, SpirvStage> spirvCache;
  std::map<std::string, BlobStage> blobCache;
  std::map<std::string, TextStage> wgslCache;
  std::map<std::tuple<std::string, uint32_t, uint32_t>, ReflectionResult> reflectionCache;
};

}  // namespace tgfx

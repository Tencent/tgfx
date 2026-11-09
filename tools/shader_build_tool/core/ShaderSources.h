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

#include <map>
#include <string>
#include <vector>
#include "gpu/shaders/PrecompiledShader.h"

namespace tgfx {

/// One registered shader with its vertex and fragment sources read from disk and every
/// #include expanded. The expanded text is exactly what the compiler receives before the variant
/// #defines are prepended.
struct ShaderSource {
  PrecompiledShaderInfo info;
  std::string vertex;
  std::string fragment;
};

/// The sources of every registered shader, read once. A build compiles from this set and computes
/// the bundle's source digest from the same set, so the digest always describes the text that was
/// actually compiled even if the files change while the build runs.
struct ShaderSourceSet {
  /// False when any shader or include could not be read; errors lists every failure.
  bool ok = false;
  std::vector<std::string> errors;
  /// Ordered by shader name, so iteration does not depend on registration order.
  std::vector<ShaderSource> shaders;
  /// How many top-level stage files came from the caller's override map instead of the disk. The
  /// caller asserts this reaches the map's size: a key the loader never matched would silently
  /// compile the handwritten file and make an equivalence check pass on the wrong text.
  size_t overrideHits = 0;

  /// Returns the shader with the given name, or nullptr.
  const ShaderSource* find(const std::string& name) const;
};

/// Reads the vertex and fragment file of every registered shader under shaderDir and expands their
/// includes. A shader file that is missing or empty, an include that cannot be read, an include
/// cycle, or nesting deeper than MaxIncludeDepth is an error: the build must not silently compile
/// a shader with a hole in it.
///
/// stageOverrides, when given, maps a family's file (its PrecompiledShaderInfo vertexFile or
/// fragmentFile) to text that replaces the file's content before the include expansion. The
/// declaration emitter uses it to compile the generated entries through the very same loader.
ShaderSourceSet LoadShaderSources(const std::string& shaderDir,
                                  const std::map<std::string, std::string>* stageOverrides = nullptr);

/// The deepest include nesting LoadShaderSources accepts.
inline constexpr int MaxIncludeDepth = 32;

/// Expands every #include of an assembled stage text against shaderDir — the same expansion
/// LoadShaderSources applies to the handwritten files, so the assembled variants and the legacy
/// families compile through identical preprocessing. Returns an empty string on failure.
std::string ExpandStageIncludes(const std::string& source, const std::string& shaderDir,
                                std::string* error);

/// The contract headers whose content the generated shader text derives from: the operator table
/// and the data-pool field lists it expands into. They are C++ headers, so no stage includes them
/// and the loader above never sees them; their text must still reach the source digest, or a
/// layout change would look like a fresh bundle.
inline constexpr const char* GeneratorContractFiles[] = {"contract/ChainISA.h",
                                                         "contract/ChainOps.h"};

/// Reads GeneratorContractFiles from the contract directory beside shaderDir (shaderDir points at
/// .../shaders/glsl; the contract headers live in .../shaders/contract) and concatenates them in
/// declaration order. Returns false with an error when one cannot be read: a digest that silently
/// omitted them would call a stale bundle fresh.
bool ReadGeneratorContract(const std::string& shaderDir, std::string* text, std::string* error);

}  // namespace tgfx

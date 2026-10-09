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

  /// Returns the shader with the given name, or nullptr.
  const ShaderSource* find(const std::string& name) const;
};

/// Reads the vertex and fragment file of every registered shader under shaderDir and expands their
/// includes. A shader file that is missing or empty, an include that cannot be read, an include
/// cycle, or nesting deeper than kMaxIncludeDepth is an error: the build must not silently compile
/// a shader with a hole in it.
ShaderSourceSet LoadShaderSources(const std::string& shaderDir);

/// The deepest include nesting LoadShaderSources accepts.
inline constexpr int kMaxIncludeDepth = 32;

}  // namespace tgfx

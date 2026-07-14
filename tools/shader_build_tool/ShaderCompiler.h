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

#include <cstdint>
#include <string>
#include <vector>

namespace tgfx {

enum class ShaderStageType { Vertex, Fragment };

struct CompileResult {
  bool success = false;
  std::vector<uint32_t> spirv;
  std::string msl;
  std::string wgsl;
  std::string error;
};

/// Compiles GLSL source (already with #defines prepended) to SPIR-V using shaderc.
/// When optimize is true, applies performance-level optimization to reduce SPIR-V size.
CompileResult CompileGLSL(const std::string& source, ShaderStageType stage,
                          const std::string& shaderName, uint32_t variantIndex,
                          bool optimize = false);

/// Translates SPIR-V binary to Metal Shading Language via spirv-cross.
CompileResult TranslateToMSL(const std::vector<uint32_t>& spirv, ShaderStageType stage);

/// Translates SPIR-V binary to WGSL via tint.
CompileResult TranslateToWGSL(const std::vector<uint32_t>& spirv);

/// Compiles MSL source text to Metal library binary (.metallib) using xcrun metal/metallib.
/// Returns empty vector on failure.
std::vector<uint8_t> CompileMSLToMetallib(const std::string& mslSource, ShaderStageType stage);

/// Prepends #define directives to shader source from a list of "NAME=value" strings.
std::string PrependDefines(const std::string& source, const std::vector<std::string>& defines);

}  // namespace tgfx

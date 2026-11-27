////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License") you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <string>
#include "gpu/ir/ProgramDescription.h"
#include "gpu/ir/ProgramIR.h"
#include "tgfx/gpu/ShaderStage.h"

namespace tgfx {
class ProgramBuilder;
class ShaderBuilder;
class UniformData;

/// The single HLSL printer for a stage. It prints the stage interface as Direct3D system-value
/// declarations (a VSIn/VSOut or PSIn struct plus SV_Position/SV_Target), the uniform block as a
/// cbuffer whose members are pinned to their std140 placements, and the main body from the
/// ProgramIR statements — the same statements the GLSL printer renders, spelled in HLSL.
class HLSLPrinter {
 public:
  /// Uniquifies the printable value names in place, under the same rule the GLSL printer applies:
  /// values two emitters named the same get stable generated names, so a printed reference and a
  /// printed definition can only match when they are the same value. Values the interface owns
  /// (uniforms, varyings, samplers, attributes) keep their names.
  static void UniquifyValueNames(ProgramIR& program);

  /// Prints the fragment main body from the ProgramIR statements: one declaration or assignment per
  /// statement, expressions through the operator emitter. Returns an empty string when a statement
  /// cannot be printed. The caller owns the closing brace and the return epilogue.
  static std::string PrintFragmentBodyFromIR(const ProgramIR& program);

  /// Prints the vertex main body from the ProgramIR vertex statements under the same rules.
  static std::string PrintVertexBodyFromIR(const ProgramIR& program);

  /// Prints the complete stage: the uniform block, the sampler bindings, the stage interface
  /// structs, the function section and main. `uniformLayout` supplies the std140 placements the
  /// cbuffer members are pinned to; without it the block is still printed but every member logs a
  /// missing placement, because the packing must not be guessed.
  static std::string PrintStage(const ProgramBuilder& programBuilder, ShaderStage stage,
                                const InterfaceDesc& interface, const ShaderBuilder& builder,
                                const UniformData* uniformLayout);
};

}  // namespace tgfx

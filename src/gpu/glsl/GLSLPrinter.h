/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License") you may not use this file except
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
#include "gpu/ir/ProgramIR.h"
#include "gpu/ir/ProgramDescription.h"
#include "gpu/shaders/contract/ShaderLibraryBundleFormat.h"
#include <vector>
#include "gpu/ir/ProgramIR.h"
#include "tgfx/gpu/ShaderStage.h"

namespace tgfx {
class ProgramBuilder;
class ShaderBuilder;

/// The single GLSL printer for a stage. It prints the interface declarations from the ProgramIR
/// interface, appends the operator function section, and prints the main body from the IR
/// statements. The result is the complete shader text sent to the driver.
class GLSLPrinter {
 public:
  /// Prints the fragment main body straight from the ProgramIR statements (the IR printer
  /// route): one declaration or assignment per statement, expressions through the operator
  /// emitter. Returns an empty string when a statement cannot be printed yet, so the caller can
  /// fall back to the recorded text route. outputName seeds the swizzle epilogue's target.
  /// Uniquifies the printable value names in place: values two emitters named the same (or left
  /// nameless) get stable generated names, so a printed reference and a printed definition can
  /// only match when they are the same value. Values the interface owns (uniforms, varyings,
  /// samplers, attributes) and the builtin outputs keep their names.
  static void UniquifyValueNames(ProgramIR& program);

  static std::string PrintFragmentBodyFromIR(const ProgramIR& program);

  /// Prints the vertex main body from the ProgramIR vertex statements under the same rules.
  static std::string PrintVertexBodyFromIR(const ProgramIR& program);

  static std::string PrintStage(const ProgramBuilder& programBuilder, ShaderStage stage,
                                const InterfaceIR& interface, const ShaderBuilder& builder);
};

}  // namespace tgfx

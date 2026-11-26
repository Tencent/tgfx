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
#include <vector>
#include "gpu/ir/ProgramDescription.h"
#include "tgfx/gpu/ShaderStage.h"

namespace tgfx {
class ProgramBuilder;
class ShaderBuilder;

/// The single GLSL printer for a stage. It prints the interface declarations from the typed
/// description, appends the recorded function section, and prints the main body from the stage
/// records. The result is the complete shader text sent to the driver.
class GLSLPrinter {
 public:
  static std::string PrintVertexProgram(const std::vector<VertexCall>& calls);

  static std::string PrintFragmentProgram(const FragmentProgram& program);

  /// Prints the fragment main body straight from the ProgramIR statements (the IR printer
  /// route): one declaration or assignment per statement, expressions through the operator
  /// emitter. Returns an empty string when a statement cannot be printed yet, so the caller can
  /// fall back to the recorded text route. outputName seeds the swizzle epilogue's target.
  static std::string PrintFragmentBodyFromIR(const ProgramIR& program);

  /// Prints the vertex main body from the ProgramIR vertex statements under the same rules.
  static std::string PrintVertexBodyFromIR(const ProgramIR& program);

  static std::string PrintStage(const ProgramBuilder& programBuilder, ShaderStage stage,
                                const InterfaceDesc& interface, const ShaderBuilder& builder);
};

}  // namespace tgfx

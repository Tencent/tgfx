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

  static std::string PrintStage(const ProgramBuilder& programBuilder, ShaderStage stage,
                                const InterfaceDesc& interface, const ShaderBuilder& builder);
};

}  // namespace tgfx

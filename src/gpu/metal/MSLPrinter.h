////////////////////////////////////////////////////////////////////////////////////////////////
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
//  license is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "gpu/ir/ProgramIR.h"

#include <set>
#include <string>
#include <vector>
#include "gpu/ir/ProgramDescription.h"
#include "tgfx/gpu/ShaderStage.h"

namespace tgfx {
class ProgramBuilder;
class ShaderBuilder;

/**
 * The single MSL printer for a stage: the interface becomes a stage-in struct and one uniform
 * struct per block, the recorded calls are spelled as MSL statements, and the function section
 * arrives already translated (the library bundle's MSL section). The result is the complete MSL
 * text the Metal runtime compiles, entry point main0.
 */
/// The call-site spelling rules the printer needs: the declared sampler names (a sampler argument
/// splits into the texture/sampler pair) and the catalog functions the translator split that way
/// (their call site names the Impl form).
struct MslCallSite {
  std::set<std::string> samplerNames = {};
  std::set<std::string> samplerFunctions = {};
};

class MSLPrinter {
 public:
  static std::string PrintVertexProgram(const std::vector<VertexCall>& calls,
                                        const MslCallSite& callSite);

  static std::string PrintFragmentProgram(const FragmentProgram& program,
                                          const MslCallSite& callSite);

  /// Prints the stage bodies straight from the ProgramIR statements, spelled in MSL. The stage
  /// shell (structs, uniform block, prologue and epilogue) still comes from PrintStage.
  static std::string PrintVertexBodyFromIR(const ProgramIR& program);

  static std::string PrintFragmentBodyFromIR(const ProgramIR& program);

  static std::string PrintStage(const ProgramBuilder& programBuilder, ShaderStage stage,
                                const InterfaceDesc& interface, const ShaderBuilder& builder,
                                const MslCallSite& callSite);
};

}  // namespace tgfx

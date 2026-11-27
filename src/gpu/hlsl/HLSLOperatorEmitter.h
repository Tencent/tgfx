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
#include "gpu/ir/ProgramIR.h"

namespace tgfx {

/// Lowers one ProgramIR operator call to the HLSL expression the manifest declares. The operator
/// library carries an HLSL section keyed by the same function name the GLSL source declares, so a
/// call site spells the same symbol with the same argument order; only the surrounding expression
/// forms (constructors, conversions, the texture reader's extra sampler argument) differ.
class HLSLOperatorEmitter {
 public:
  /// Emits the expression for the call that defines result. Returns false when the value is not a
  /// call, the operator is unknown, or an argument has no printable name.
  static bool EmitCall(const ProgramIR& program, ValueID result, std::string* expression,
                       std::string* error = nullptr);

  /// Emits the value as an HLSL expression, ignoring its debug name when it has a defining
  /// expression. Resource values still use their debug names.
  static bool EmitValue(const ProgramIR& program, ValueID value, std::string* expression,
                        std::string* error = nullptr);

  /// The HLSL spelling of one IR type, or nullptr when the type has no HLSL form.
  static const char* TypeName(SLType type);

  /// The sampler-state variable the stage declares beside one texture variable. Direct3D binds the
  /// sampler separately from the texture, so every read passes the pair at the call site.
  static std::string SamplerStateName(const std::string& textureName);
};

}  // namespace tgfx

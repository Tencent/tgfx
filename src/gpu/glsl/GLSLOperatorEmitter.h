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

namespace tgfx {

/// Lowers one ProgramIR operator call to the GLSL expression the manifest declares. Values keep
/// their debug names because the GL path still prints those names verbatim.
class GLSLOperatorEmitter {
 public:
  /// Emits the expression for the call that defines result. Returns false when the value is not a
  /// call, the operator is unknown, or an argument has no printable name.
  static bool EmitCall(const ProgramIR& program, ValueID result, std::string* expression,
                       std::string* error = nullptr);
};

}  // namespace tgfx

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
#include "gpu/ir/OperatorBundle.h"
#include "gpu/ir/ProgramIR.h"
#include "gpu/spirv/SPIRVShaderBuilder.h"

namespace tgfx {

/// The SPIR-V counterpart of GLSLOperatorEmitter. It validates the manifest call, finds the
/// precompiled bundle function and emits OpFunctionCall. It does not emit operator bodies.
class SPIRVOperatorEmitter {
 public:
  static bool EmitCall(const ProgramIR& program, ValueID result, const OperatorBundle& bundle,
                       SPIRVShaderBuilder* builder, std::string* error = nullptr);
};

}  // namespace tgfx

/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "GLSLEmptyXferProcessor.h"
#include "gpu/ir/ProgramIRBuilder.h"

namespace tgfx {
const EmptyXferProcessor* EmptyXferProcessor::GetInstance() {
  static auto& xferProcessor = *new GLSLEmptyXferProcessor();
  return &xferProcessor;
}

void GLSLEmptyXferProcessor::emitCode(const EmitArgs& args) const {
  if (args.irBuilder != nullptr && args.outputValue != INVALID_VALUE_ID) {
    // A draw with no color or coverage processor leaves the corresponding input unset; the text
    // route treats that as white, so the IR names the same constant.
    ValueID inputColor = args.inputColorValue;
    if (inputColor == INVALID_VALUE_ID) {
      const auto one = args.irBuilder->CreateConstant(1.0f);
      inputColor = args.irBuilder->CreateConstruct(SLType::Float4, {one, one, one, one});
    }
    ValueID inputCoverage = args.inputCoverageValue;
    if (inputCoverage == INVALID_VALUE_ID) {
      const auto one = args.irBuilder->CreateConstant(1.0f);
      inputCoverage = args.irBuilder->CreateConstruct(SLType::Float4, {one, one, one, one});
    }
    const auto result =
        args.irBuilder->CreateBinary(IRBinaryOp::Mul, SLType::Float4, inputColor, inputCoverage);
    args.irBuilder->StoreOutput(args.outputValue, result);
  }
  args.fragBuilder->combineColorWithCoverage(args.outputColor, args.inputColor, args.inputCoverage);
}
}  // namespace tgfx

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

namespace tgfx {
const EmptyXferProcessor* EmptyXferProcessor::GetInstance() {
  static auto& xferProcessor = *new GLSLEmptyXferProcessor();
  return &xferProcessor;
}

void GLSLEmptyXferProcessor::emitCode(const EmitArgs& args) const {
  args.fragBuilder->codeAppendf("%s = %s * %s;", args.outputColor.c_str(), args.inputColor.c_str(),
                                args.inputCoverage.c_str());
}
}  // namespace tgfx

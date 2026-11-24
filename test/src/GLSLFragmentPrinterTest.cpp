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

#include <string>
#include "gpu/glsl/GLSLFragmentPrinter.h"
#include "gtest/gtest.h"

namespace tgfx {

TEST(GLSLFragmentPrinterTest, ReplaysRawTextAndClosesMain) {
  FragmentProgram program = {};
  FragmentInstruction first = {};
  first.kind = FragmentInstructionKind::RawText;
  first.text = "    vec4 color = vec4(1.0);\n";
  program.instructions.push_back(first);

  FragmentInstruction call = {};
  call.kind = FragmentInstructionKind::Call;
  call.output = "result";
  call.function = "pmApplyLuma";
  program.instructions.push_back(call);

  FragmentInstruction second = {};
  second.kind = FragmentInstructionKind::RawText;
  second.text = "    color *= result;\n";
  program.instructions.push_back(second);

  const std::string text = PrintGLSLFragmentProgram(program);
  EXPECT_EQ(text, "    vec4 color = vec4(1.0);\n    color *= result;\n}\n");
}

}  // namespace tgfx

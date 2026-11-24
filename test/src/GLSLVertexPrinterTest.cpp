/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include <string>
#include <vector>
#include "gpu/glsl/GLSLPrinter.h"
#include "gtest/gtest.h"

namespace tgfx {

TEST(GLSLVertexPrinterTest, PrintsAffineAndPosition) {
  std::vector<VertexCall> calls = {};
  VertexCall transform = {};
  transform.kind = VertexCallKind::TransformPoint;
  transform.dst = "local";
  transform.src = "position";
  transform.operand = "Matrix";
  calls.push_back(transform);

  VertexCall position = {};
  position.kind = VertexCallKind::StorePosition;
  position.src = "position";
  calls.push_back(position);

  const std::string text = GLSLPrinter::PrintVertexProgram(calls);
  EXPECT_NE(text.find("highp vec2 local = (Matrix * vec3(position, 1.0)).xy;"), std::string::npos);
  EXPECT_NE(text.find("gl_Position = vec4(position.xy * tgfx_RTAdjust.xz"), std::string::npos);
  EXPECT_EQ(text.back(), '\n');
}

TEST(GLSLVertexPrinterTest, PrintsPerspectiveTransform) {
  std::vector<VertexCall> calls = {};
  VertexCall transform = {};
  transform.kind = VertexCallKind::TransformPoint;
  transform.dst = "uv";
  transform.src = "position";
  transform.operand = "Matrix";
  transform.hasPerspective = true;
  calls.push_back(transform);

  const std::string text = GLSLPrinter::PrintVertexProgram(calls);
  EXPECT_NE(text.find("highp vec3 uvTemp = Matrix * vec3(position, 1.0);"), std::string::npos);
  EXPECT_NE(text.find("highp vec2 uv = uvTemp.xy / uvTemp.z;"), std::string::npos);
}

}  // namespace tgfx

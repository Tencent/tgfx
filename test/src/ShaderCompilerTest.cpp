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
#include <unordered_map>
#include "gpu/ShaderCompiler.h"
#include "gtest/gtest.h"
#include "utils/TestUtils.h"

namespace tgfx {

static constexpr char VERTEX_SHADER[] = R"(
#version 450
layout(location = 0) in vec3 aPosition;
out float vRed;
out float vGreen;
out float vBlue;
void main() {
    gl_Position = vec4(aPosition, 1.0);
}
)";

static constexpr char FRAGMENT_SHADER[] = R"(
#version 450
in float vBlue;
in float vRed;
in float vGreen;
layout(location = 0) out vec4 fragColor;
void main() {
    fragColor = vec4(vRed, vGreen, vBlue, 1.0);
}
)";

TGFX_TEST(ShaderCompilerTest, ExtractVertexOutputs) {
  auto decls = ExtractVaryingDecls(VERTEX_SHADER, ShaderStage::Vertex);
  EXPECT_EQ(decls.size(), 3u);
  EXPECT_EQ(decls["vRed"], 1);
  EXPECT_EQ(decls["vGreen"], 1);
  EXPECT_EQ(decls["vBlue"], 1);
}

TGFX_TEST(ShaderCompilerTest, ExtractFragmentInputs) {
  auto decls = ExtractVaryingDecls(FRAGMENT_SHADER, ShaderStage::Fragment);
  EXPECT_EQ(decls.size(), 3u);
  EXPECT_EQ(decls["vRed"], 1);
  EXPECT_EQ(decls["vGreen"], 1);
  EXPECT_EQ(decls["vBlue"], 1);
}

TGFX_TEST(ShaderCompilerTest, ExtractArrayVaryings) {
  static constexpr char kVertexShader[] = R"(
#version 450
out vec2 vUVs[2];
out float vAlpha;
void main() {}
)";
  auto decls = ExtractVaryingDecls(kVertexShader, ShaderStage::Vertex);
  EXPECT_EQ(decls.size(), 2u);
  EXPECT_EQ(decls["vUVs"], 2);
  EXPECT_EQ(decls["vAlpha"], 1);
}

TGFX_TEST(ShaderCompilerTest, ExtractWithQualifiers) {
  static constexpr char kVertexShader[] = R"(
#version 450
flat out highp float vId;
noperspective out mediump vec2 vUV;
out lowp float vAlpha;
void main() {}
)";
  auto decls = ExtractVaryingDecls(kVertexShader, ShaderStage::Vertex);
  EXPECT_EQ(decls.size(), 3u);
  EXPECT_EQ(decls["vId"], 1);
  EXPECT_EQ(decls["vUV"], 1);
  EXPECT_EQ(decls["vAlpha"], 1);
}

TGFX_TEST(ShaderCompilerTest, VaryingInterfacesMatch) {
  auto vertexDecls = ExtractVaryingDecls(VERTEX_SHADER, ShaderStage::Vertex);
  auto fragmentDecls = ExtractVaryingDecls(FRAGMENT_SHADER, ShaderStage::Fragment);
  std::string mismatch;
  EXPECT_TRUE(VaryingInterfacesMatch(vertexDecls, fragmentDecls, mismatch));
  EXPECT_TRUE(mismatch.empty());
}

TGFX_TEST(ShaderCompilerTest, ExtraOutputMismatch) {
  static constexpr char kVertexShader[] = R"(
#version 450
out float vUsed;
out float vUnused;
void main() {}
)";
  static constexpr char kFragmentShader[] = R"(
#version 450
in float vUsed;
void main() {}
)";
  auto vertexDecls = ExtractVaryingDecls(kVertexShader, ShaderStage::Vertex);
  auto fragmentDecls = ExtractVaryingDecls(kFragmentShader, ShaderStage::Fragment);
  std::string mismatch;
  EXPECT_FALSE(VaryingInterfacesMatch(vertexDecls, fragmentDecls, mismatch));
  EXPECT_EQ(mismatch, "vertex output 'vUnused' is not consumed by the fragment");
}

TGFX_TEST(ShaderCompilerTest, ExtraInputMismatch) {
  static constexpr char kVertexShader[] = R"(
#version 450
out float vUsed;
void main() {}
)";
  static constexpr char kFragmentShader[] = R"(
#version 450
in float vUsed;
in float vExtra;
void main() {}
)";
  auto vertexDecls = ExtractVaryingDecls(kVertexShader, ShaderStage::Vertex);
  auto fragmentDecls = ExtractVaryingDecls(kFragmentShader, ShaderStage::Fragment);
  std::string mismatch;
  EXPECT_FALSE(VaryingInterfacesMatch(vertexDecls, fragmentDecls, mismatch));
  EXPECT_EQ(mismatch, "fragment input 'vExtra' has no matching vertex output");
}

TGFX_TEST(ShaderCompilerTest, ArrayLengthMismatch) {
  static constexpr char kVertexShader[] = R"(
#version 450
out float vColor[2];
void main() {}
)";
  static constexpr char kFragmentShader[] = R"(
#version 450
in float vColor[1];
void main() {}
)";
  auto vertexDecls = ExtractVaryingDecls(kVertexShader, ShaderStage::Vertex);
  auto fragmentDecls = ExtractVaryingDecls(kFragmentShader, ShaderStage::Fragment);
  std::string mismatch;
  EXPECT_FALSE(VaryingInterfacesMatch(vertexDecls, fragmentDecls, mismatch));
  EXPECT_EQ(mismatch, "varying 'vColor' has an inconsistent array length");
}

}  // namespace tgfx

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
//  Unless required by applicable law or agreed to in writing, software distributed under
//  the license is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
//  KIND, either express or implied. see the license for the specific language governing
//  permissions and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

// The direct-GLSL emitters are the only code that produces the text the GL and GLES bundles
// store, and a rewrite that changes a single byte of it changes every stored variant. These
// tests pin the folding, the dialect rewrites and the leak scan at the unit level, so the
// byte-parity gate catches only what a genuinely new input form produces.
//
// FoldIntegerExpression pins two things beyond arithmetic: a literal beyond the int64 range
// fails the fold instead of throwing (the tool builds with exceptions disabled), and trailing
// garbage fails it so the caller keeps the original text.

#include <cstdint>
#include <string>
#include "emit/FormCheck.h"
#include "emit/GLSLDialect.h"
#include "gtest/gtest.h"

namespace tgfx {
namespace {

// The smallest fragment template that exercises every rewrite: a UBO with set/binding members,
// a located output, a macro-built location id, and a varying input.
const char* const FragmentTemplate = R"(#version 450
#define OUT_ID (1 + 1)
layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  float uValue;
};
layout(location = 0) in vec2 vUV;
layout(location = OUT_ID) out vec4 fragColor;
void main() {
  fragColor = vec4(uValue, vUV, 1.0);
}
)";

TEST(FoldIntegerExpressionTest, EvaluatesArithmeticWithPrecedenceAndParentheses) {
  int64_t value = 0;
  ASSERT_TRUE(FoldIntegerExpression("1+2*3", &value));
  EXPECT_EQ(value, 7);
  ASSERT_TRUE(FoldIntegerExpression("(1+2)*3", &value));
  EXPECT_EQ(value, 9);
  ASSERT_TRUE(FoldIntegerExpression("10-2-3", &value));
  EXPECT_EQ(value, 5);
  ASSERT_TRUE(FoldIntegerExpression(" 2 * ( 3 + 4 ) ", &value));
  EXPECT_EQ(value, 14);
}

TEST(FoldIntegerExpressionTest, EvaluatesUnaryOperatorsAndZero) {
  int64_t value = 0;
  ASSERT_TRUE(FoldIntegerExpression("-5+2", &value));
  EXPECT_EQ(value, -3);
  ASSERT_TRUE(FoldIntegerExpression("+7", &value));
  EXPECT_EQ(value, 7);
  ASSERT_TRUE(FoldIntegerExpression("0", &value));
  EXPECT_EQ(value, 0);
}

TEST(FoldIntegerExpressionTest, RejectsAnythingThatIsNotAnIntegerExpression) {
  int64_t value = 0;
  EXPECT_FALSE(FoldIntegerExpression("", &value));
  EXPECT_FALSE(FoldIntegerExpression("a", &value));
  EXPECT_FALSE(FoldIntegerExpression("1+", &value));
  EXPECT_FALSE(FoldIntegerExpression("1 2", &value));
  EXPECT_FALSE(FoldIntegerExpression("1)", &value));
  EXPECT_FALSE(FoldIntegerExpression("2**3", &value));
  EXPECT_FALSE(FoldIntegerExpression("(1", &value));
}

TEST(FoldIntegerExpressionTest, RejectsALiteralBeyondTheInt64Range) {
  // std::stoll threw on this input; the fold must fail closed so the original text is kept.
  int64_t value = 0;
  EXPECT_FALSE(FoldIntegerExpression("9223372036854775808", &value));
  EXPECT_FALSE(FoldIntegerExpression("-9223372036854775809", &value));
}

TEST(EmitDirectGLSLTest, RewritesTheHeaderDropsSetAndBindingAndFoldsLocations) {
  std::string error;
  auto text = EmitDirectGLSL330(FragmentTemplate, ShaderStageType::Fragment, &error);
  EXPECT_TRUE(error.empty()) << error;
  ASSERT_FALSE(text.empty());
  EXPECT_NE(text.find("#version 330"), std::string::npos);
  EXPECT_EQ(text.find("#version 450"), std::string::npos);
  EXPECT_EQ(FindDirectFormLeak(text, false).reason, nullptr) << text;
  // The macro-built output location folds to a literal 2 and survives as a located output.
  EXPECT_NE(text.find("layout(location = 2) out vec4 fragColor"), std::string::npos) << text;
  // The varying input loses its location member: fragment `in` links by name in 330.
  EXPECT_EQ(text.find("layout(location = 0) in"), std::string::npos) << text;
  EXPECT_NE(text.find("in vec2 vUV"), std::string::npos) << text;
}

TEST(EmitDirectGLSLTest, FailsClosedWithoutAVersion450Header) {
  std::string error;
  EXPECT_TRUE(EmitDirectGLSL330("void main() {}", ShaderStageType::Fragment, &error).empty());
  // The failure carries its reason to the caller instead of printing it.
  EXPECT_FALSE(error.empty());
  EXPECT_NE(error.find("preprocessing failed"), std::string::npos) << error;
}

TEST(EmitDirectGLSTest, AddsTheESHeaderPrecisionsAndHighpDecoration) {
  std::string error;
  auto text = EmitDirectGLSLES300(FragmentTemplate, ShaderStageType::Fragment, false, &error);
  EXPECT_TRUE(error.empty()) << error;
  ASSERT_FALSE(text.empty());
  EXPECT_NE(text.find("#version 300 es"), std::string::npos);
  EXPECT_NE(text.find("precision mediump float;"), std::string::npos);
  EXPECT_NE(text.find("precision highp int;"), std::string::npos);
  EXPECT_EQ(FindDirectFormLeak(text, true).reason, nullptr) << text;
  // Every float-family declaration carries highp: the numerical contract of the ES dialect.
  EXPECT_NE(text.find("highp float uValue"), std::string::npos) << text;
  EXPECT_NE(text.find("highp vec4 fragColor"), std::string::npos) << text;
}

TEST(EmitDirectGLSTest, RewritesTheFramebufferFetchDialectOnFbfVariants) {
  const char* const FbfTemplate = R"(#version 450
layout(input_attachment_index = 0, set = 2, binding = 0) uniform subpassInput tgfx_SubpassInput;
layout(location = 0) out vec4 fragColor;
void main() {
  vec4 dstColor = subpassLoad(tgfx_SubpassInput);
  fragColor = dstColor;
}
)";
  std::string error;
  auto text = EmitDirectGLSLES300(FbfTemplate, ShaderStageType::Fragment, true, &error);
  EXPECT_TRUE(error.empty()) << error;
  ASSERT_FALSE(text.empty());
  EXPECT_EQ(text.find("subpassInput"), std::string::npos) << text;
  EXPECT_EQ(text.find("subpassLoad"), std::string::npos) << text;
  EXPECT_NE(text.find("#extension GL_EXT_shader_framebuffer_fetch : require"), std::string::npos)
      << text;
  EXPECT_NE(text.find("layout(location = 0) inout"), std::string::npos) << text;
  EXPECT_EQ(FindDirectFormLeak(text, true).reason, nullptr) << text;
}

TEST(FindDirectFormLeakTest, AcceptsACleanTextOfEitherDialect) {
  const std::string gl = "#version 330\nlayout(location = 0) out vec4 fragColor;\n"
                         "void main() { fragColor = vec4(1.0); }\n";
  EXPECT_EQ(FindDirectFormLeak(gl, false).reason, nullptr);

  const std::string es = "#version 300 es\nprecision mediump float;\nprecision highp int;\n"
                         "highp vec4 fragColor;\nvoid main() { fragColor = vec4(1.0); }\n";
  EXPECT_EQ(FindDirectFormLeak(es, true).reason, nullptr);
}

TEST(FindDirectFormLeakTest, ReportsTheWrongDialectHeader) {
  const std::string esText = "#version 300 es\nprecision mediump float;\n";
  EXPECT_STREQ(FindDirectFormLeak(esText, false).reason, "dialect header");

  const std::string glText = "#version 330\n";
  EXPECT_STREQ(FindDirectFormLeak(glText, true).reason, "dialect header");

  const std::string esWithoutPrecision = "#version 300 es\n";
  EXPECT_STREQ(FindDirectFormLeak(esWithoutPrecision, true).reason, "dialect header");
}

TEST(FindDirectFormLeakTest, ReportsSurvivingQualifiersWithTheirOffset) {
  const std::string withSet =
      "#version 330\nlayout(std140, set = 0, binding = 1) uniform B { float x; };\n";
  auto setLeak = FindDirectFormLeak(withSet, false);
  EXPECT_STREQ(setLeak.reason, "surviving set");
  EXPECT_GT(setLeak.offset, 0u);
  EXPECT_EQ(withSet.compare(setLeak.offset, 4, "set "), 0);

  const std::string withBinding = "#version 330\nlayout(binding = 1) uniform sampler2D s;\n";
  auto bindingLeak = FindDirectFormLeak(withBinding, false);
  EXPECT_STREQ(bindingLeak.reason, "surviving binding");
  EXPECT_EQ(withBinding.compare(bindingLeak.offset, 8, "binding "), 0);
}

TEST(FindDirectFormLeakTest, IgnoresAnAssignmentWhoseNameContainsSetOrBinding) {
  // `offset = ` contains "set = " as a substring; only a layout-qualifier context counts
  // (the token preceded by '(' or ','), so a plain assignment is not a leak.
  const std::string text = "#version 330\nvoid main() { float offset = 1.0; }\n";
  EXPECT_EQ(FindDirectFormLeak(text, false).reason, nullptr);
}

TEST(FindDirectFormLeakTest, ReportsSurvivingConditionalsAndSubpassSyntax) {
  const std::string conditional = "#version 330\n#ifdef X\nvoid main() {}\n#endif\n";
  EXPECT_STREQ(FindDirectFormLeak(conditional, false).reason, "surviving conditional");

  const std::string subpass = "#version 300 es\nprecision mediump float;\n"
                              "vec4 c = subpassLoad(tgfx_SubpassInput);\n";
  EXPECT_STREQ(FindDirectFormLeak(subpass, true).reason, "subpass syntax");
}

}  // namespace
}  // namespace tgfx

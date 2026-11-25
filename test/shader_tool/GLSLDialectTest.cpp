/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

// FindDirectFormLeak pins the text-level checks every stored direct-GL variant must pass: the
// dialect header must match the declared form, and no set/binding qualifier, conditional block
// or subpass syntax may survive the rewrite.

#include <string>
#include "gtest/gtest.h"
#include "verify/BindingLayoutRule.h"

namespace tgfx {
namespace {

TEST(FindDirectFormLeakTest, AcceptsACleanTextOfEitherDialect) {
  const std::string gl =
      "#version 330\nlayout(location = 0) out vec4 fragColor;\n"
      "void main() { fragColor = vec4(1.0); }\n";
  EXPECT_EQ(FindDirectFormLeak(gl, false).reason, nullptr);

  const std::string es =
      "#version 300 es\nprecision mediump float;\nprecision highp int;\n"
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

  const std::string subpass =
      "#version 300 es\nprecision mediump float;\n"
      "vec4 c = subpassLoad(tgfx_SubpassInput);\n";
  EXPECT_STREQ(FindDirectFormLeak(subpass, true).reason, "subpass syntax");
}

}  // namespace
}  // namespace tgfx

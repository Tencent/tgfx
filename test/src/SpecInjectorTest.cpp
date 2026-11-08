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
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

// The injector registry is the one place a code format joins the specialization channel. A format
// wired to the wrong channel either rewrites text the backend cannot recompile or ships a
// constant table the pipeline never reads, and both failures degrade silently to the generic form.
// These tests pin the matrix, the generic form's "inject nothing" answer, and the value layout
// the pipeline-constant backends resolve by SpecId.

#include <cstdint>
#include <string>
#include <vector>
#include "base/TGFXTest.h"
#include "gpu/aot/spec/SpecInjector.h"
#include "gpu/aot/spec/SpecValues.h"
#include "gtest/gtest.h"

namespace tgfx {
namespace {

SpecFieldInfo MakeField(const char* name, uint32_t arraySize, uint32_t firstId) {
  SpecFieldInfo field;
  field.name = name;
  field.arraySize = arraySize;
  field.firstId = firstId;
  return field;
}

SpecValues MakeSpecialized(std::vector<int32_t> values) {
  SpecValues result;
  result.values = std::move(values);
  return result;
}

const char* const GlslText = "#version 450\nvoid main() {}\n";

}  // namespace

TGFX_TEST(SpecInjectorTest, TheFormatMatrixPicksOneChannelPerFormat) {
  EXPECT_NE(FindSpecInjector(ShaderCodeFormat::GLSL), nullptr);
  EXPECT_NE(FindSpecInjector(ShaderCodeFormat::HLSL), nullptr);
  auto pipeline = FindSpecInjector(ShaderCodeFormat::METALLIB);
  ASSERT_NE(pipeline, nullptr);
  EXPECT_EQ(FindSpecInjector(ShaderCodeFormat::SPIRV), pipeline);
  EXPECT_EQ(FindSpecInjector(ShaderCodeFormat::WGSL), pipeline);
  EXPECT_EQ(FindSpecInjector(ShaderCodeFormat::MSL), nullptr);
  EXPECT_EQ(FindSpecInjector(ShaderCodeFormat::DXBC), nullptr);
}

TGFX_TEST(SpecInjectorTest, GlslMacrosFollowTheVersionDirective) {
  auto injector = FindSpecInjector(ShaderCodeFormat::GLSL);
  std::vector<SpecFieldInfo> fields = {MakeField("SlotCount", 1, 1)};
  SpecInjection injection;
  ASSERT_TRUE(injector->Apply(GlslText, fields, MakeSpecialized({1, 7}), &injection));
  EXPECT_TRUE(injection.constants.empty());
  const auto versionEnd = injection.code.find("#version 450\n");
  ASSERT_NE(versionEnd, std::string::npos);
  const auto masterDefine = injection.code.find("#define TGFX_SPEC_0 1");
  ASSERT_NE(masterDefine, std::string::npos);
  EXPECT_LT(versionEnd, masterDefine);
  EXPECT_NE(injection.code.find("#define TGFX_SPEC_1 7"), std::string::npos);
}

TGFX_TEST(SpecInjectorTest, HlslMacrosPrependAtTheTopWithTheSpirvCrossPrefix) {
  auto injector = FindSpecInjector(ShaderCodeFormat::HLSL);
  std::vector<SpecFieldInfo> fields = {MakeField("SlotCount", 1, 1)};
  SpecInjection injection;
  ASSERT_TRUE(injector->Apply("// hlsl\nfloat4 main() { return 0; }\n", fields,
                              MakeSpecialized({1, 3}), &injection));
  EXPECT_EQ(injection.code.rfind("#define SPIRV_CROSS_CONSTANT_ID_0 1", 0), 0u);
  EXPECT_NE(injection.code.find("#define SPIRV_CROSS_CONSTANT_ID_1 3"), std::string::npos);
}

TGFX_TEST(SpecInjectorTest, PipelineConstantsCarryTheSpecIdsAndLeaveTheCodeAlone) {
  auto injector = FindSpecInjector(ShaderCodeFormat::SPIRV);
  std::vector<SpecFieldInfo> fields = {MakeField("Scalar", 1, 1), MakeField("Packed", 3, 2)};
  SpecInjection injection;
  ASSERT_TRUE(injector->Apply(
      std::string(), fields,
      MakeSpecialized({1, 5, 10, 11, 12, 13, 20, 21, 22, 23, 30, 31, 32, 33}), &injection));
  EXPECT_TRUE(injection.code.empty());
  ASSERT_EQ(injection.constants.size(), 1u + 1u + 12u);
  EXPECT_EQ(injection.constants[0].first, "0");
  EXPECT_EQ(injection.constants[0].second, 1.0);
  EXPECT_EQ(injection.constants[1].first, "1");
  EXPECT_EQ(injection.constants[1].second, 5.0);
  EXPECT_EQ(injection.constants[2].first, "2");
  EXPECT_EQ(injection.constants.back().first, "13");
  EXPECT_EQ(injection.constants.back().second, 33.0);
}

TGFX_TEST(SpecInjectorTest, TheGenericFormInjectsNothing) {
  std::vector<SpecFieldInfo> fields = {MakeField("Scalar", 1, 1)};
  for (auto format : {ShaderCodeFormat::GLSL, ShaderCodeFormat::SPIRV}) {
    auto injector = FindSpecInjector(format);
    ASSERT_NE(injector, nullptr);
    SpecInjection injection;
    EXPECT_FALSE(injector->Apply(GlslText, fields, MakeSpecialized({}), &injection));
    EXPECT_FALSE(injector->Apply(GlslText, fields, MakeSpecialized({0, 0}), &injection));
    EXPECT_TRUE(injection.code.empty());
    EXPECT_TRUE(injection.constants.empty());
  }
}

TGFX_TEST(SpecInjectorTest, GlslWithoutAVersionLineRefuses) {
  auto injector = FindSpecInjector(ShaderCodeFormat::GLSL);
  std::vector<SpecFieldInfo> fields = {MakeField("Scalar", 1, 1)};
  SpecInjection injection;
  EXPECT_FALSE(injector->Apply("void main() {}\n", fields, MakeSpecialized({1, 1}), &injection));
}

}  // namespace tgfx

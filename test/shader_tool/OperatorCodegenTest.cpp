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

// The //@op pipeline at the unit level: the annotation parses, the parameter roles derive from
// the signature, every ambiguous form fails closed with the function named, and the generated
// glue carries the enum, the description table and the typed Uniforms structs. The generated
// sources are only asserted for content here; the runtime links them once the migration's first
// processor batch adopts the glue.

#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>
#include "cli/BuildCommand.h"
#include "gtest/gtest.h"

namespace tgfx {
namespace {

class OperatorCodegenTest : public testing::Test {
 protected:
  void SetUp() override {
    root = std::filesystem::temp_directory_path() /
           ("tgfx_opcodegen_" + std::to_string(::getpid()));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "fn");
  }

  void TearDown() override {
    std::filesystem::remove_all(root);
  }

  void WriteInc(const std::string& name, const std::string& text) {
    std::ofstream file(root / "fn" / name, std::ios::binary);
    file << text;
  }

  std::filesystem::path root = {};
};

TEST_F(OperatorCodegenTest, DerivesTheRolesOfEveryParameterKind) {
  WriteInc("op_all.inc",
           "//@op AllKinds\n"
           "//@frag\n"
           "vec4 pmAllKinds(/*@in*/ vec4 color, /*@in*/ int radius, float scale, "
           "vec4 kernel[4], sampler2D tex /*@in*/) { return color; }\n");
  std::vector<OperatorSignature> signatures = {};
  std::string error;
  ASSERT_TRUE(CollectOperatorSignatures(root.string(), &signatures, &error)) << error;
  ASSERT_EQ(signatures.size(), 1u);
  const auto& signature = signatures[0];
  EXPECT_EQ(signature.opName, "AllKinds");
  EXPECT_EQ(signature.functionName, "pmAllKinds");
  EXPECT_EQ(signature.stage, "fragment");
  ASSERT_EQ(signature.params.size(), 5u);
  EXPECT_EQ(signature.params[0].role, OperatorParamRole::In);
  EXPECT_EQ(signature.params[0].intLiteral, false);
  EXPECT_EQ(signature.params[1].role, OperatorParamRole::In);
  EXPECT_EQ(signature.params[1].intLiteral, true);
  EXPECT_EQ(signature.params[2].role, OperatorParamRole::Uniform);
  EXPECT_EQ(signature.params[3].role, OperatorParamRole::UniformArray);
  EXPECT_EQ(signature.params[3].arrayCount, 4u);
  // A marked sampler keeps the sampler role: existing entry functions (yuv.inc) mark theirs.
  EXPECT_EQ(signature.params[4].role, OperatorParamRole::Sampler);
}

TEST_F(OperatorCodegenTest, ReadsTheStageLineAndSkipsUnannotatedFunctions) {
  WriteInc("op_vert.inc",
           "//@op VertOp\n"
           "//@vert\n"
           "float pmVertOp(float value) { return value; }\n");
  WriteInc("helper.inc", "//@function pmHelper\nfloat pmHelper(float v) { return v; }\n");
  std::vector<OperatorSignature> signatures = {};
  std::string error;
  ASSERT_TRUE(CollectOperatorSignatures(root.string(), &signatures, &error)) << error;
  ASSERT_EQ(signatures.size(), 1u);
  EXPECT_EQ(signatures[0].stage, "vertex");
  EXPECT_EQ(signatures[0].functionName, "pmVertOp");
}

TEST_F(OperatorCodegenTest, TheOptionalFunctionAnnotationCrossChecksTheSignature) {
  // A matching //@function name passes and keeps feeding the legacy prefix form.
  WriteInc("op_checked.inc",
           "//@function pmChecked Chain\n"
           "//@op Checked\n"
           "//@frag\n"
           "vec4 pmChecked(float v) { return vec4(v); }\n");
  std::vector<OperatorSignature> signatures = {};
  std::string error;
  ASSERT_TRUE(CollectOperatorSignatures(root.string(), &signatures, &error)) << error;
  ASSERT_EQ(signatures.size(), 1u);
  EXPECT_EQ(signatures[0].functionName, "pmChecked");

  // A mismatching one fails closed.
  std::filesystem::remove_all(root / "fn");
  std::filesystem::create_directories(root / "fn");
  WriteInc("op_mismatch.inc",
           "//@function pmWrong\n"
           "//@op Mismatch\n"
           "//@frag\n"
           "vec4 pmMismatch(float v) { return vec4(v); }\n");
  EXPECT_FALSE(CollectOperatorSignatures(root.string(), &signatures, &error));
  EXPECT_NE(error.find("does not match the signature"), std::string::npos) << error;
}

TEST_F(OperatorCodegenTest, RejectsTheAmbiguousForms) {
  const std::string cases[] = {
      "//@op A\n//@frag\nvec4 pmA(/*@in*/ float k[4]) { return vec4(0.0); }\n",
      "//@op A\n//@frag\nvec4 pmA(Foo bar) { return vec4(0.0); }\n",
      "//@op A\n//@frag\nvec4 pmA(float m[3]) { return vec4(0.0); }\n",
      "//@op A\n//@frag\nvec4 pmA(float v) { return vec4(v); }\n"
      "//@op A\n//@frag\nvec4 pmB(float v) { return vec4(v); }\n",
      "//@op A\nfloat pmA(float v) { return v; }\n",
      "//@op A fragment\n//@frag\nfloat pmA(float v) { return v; }\n",
      "//@frag\nfloat pmA(float v) { return v; }\n",
      "//@op A\n//@frag\n//@vert\nfloat pmA(float v) { return v; }\n",
  };
  for (const auto& text : cases) {
    std::filesystem::remove_all(root / "fn");
    std::filesystem::create_directories(root / "fn");
    WriteInc("case.inc", text);
    std::vector<OperatorSignature> signatures = {};
    std::string error;
    EXPECT_FALSE(CollectOperatorSignatures(root.string(), &signatures, &error))
        << "expected a rejection for: " << text;
    EXPECT_FALSE(error.empty());
  }
}

TEST_F(OperatorCodegenTest, RejectsADuplicateOpAnnotation) {
  WriteInc("op_twice.inc",
           "//@op Twice\n//@op Twice\n//@frag\nfloat pmTwice(float v) { return v; }\n");
  std::vector<OperatorSignature> signatures = {};
  std::string error;
  EXPECT_FALSE(CollectOperatorSignatures(root.string(), &signatures, &error));
  EXPECT_FALSE(error.empty());
}

TEST_F(OperatorCodegenTest, GeneratesEnumTableAndUniforms) {
  WriteInc("op_luma.inc",
           "//@op Luma\n"
           "//@frag\n"
           "vec4 pmApplyLuma(/*@in*/ vec4 color, float kr, float kg, float kb) "
           "{ return color; }\n");
  WriteInc("op_const_color.inc",
           "//@op ConstColor\n"
           "//@frag\n"
           "vec4 pmApplyConstColor(vec4 constColor, /*@in*/ vec4 inputColor, "
           "/*@in*/ int inputMode) { return constColor; }\n");
  std::vector<OperatorSignature> signatures = {};
  std::string error;
  ASSERT_TRUE(CollectOperatorSignatures(root.string(), &signatures, &error)) << error;
  ASSERT_EQ(signatures.size(), 2u);

  const auto headerPath = root / "GeneratedOperators.h";
  const auto sourcePath = root / "GeneratedOperators.cpp";
  const auto idsPath = root / "GeneratedOperatorIDs.h";
  ASSERT_TRUE(WriteGeneratedOperators(signatures, headerPath.string(), sourcePath.string(),
                                      idsPath.string(), &error))
      << error;

  std::ifstream headerFile(headerPath);
  std::string header((std::istreambuf_iterator<char>(headerFile)),
                     std::istreambuf_iterator<char>());
  EXPECT_NE(header.find("struct ConstColorUniforms {"), std::string::npos);
  EXPECT_NE(header.find("std::array<float, 4> constColor = {};"), std::string::npos);
  EXPECT_NE(header.find("struct LumaUniforms {"), std::string::npos);
  EXPECT_NE(header.find("float kr = {};"), std::string::npos);
  EXPECT_NE(header.find("void upload(UniformData* uniformData) const;"), std::string::npos);
  EXPECT_NE(header.find("struct ConstColorOp {"), std::string::npos);
  EXPECT_NE(header.find("struct UniformNames {"), std::string::npos);
  EXPECT_NE(header.find("std::string constColor;"), std::string::npos);
  EXPECT_NE(header.find("static ValueID Emit(ProgramIRBuilder* ir, "
                        "UniformHandler* uniformHandler,"),
            std::string::npos);
  EXPECT_NE(header.find("UniformNames* textNames, ValueID inputColor, int32_t inputMode);"),
            std::string::npos);
  EXPECT_NE(header.find("key must encode: every int literal argument below (inputMode)."),
            std::string::npos);
  EXPECT_NE(header.find("UniformNames* textNames, ValueID color);"), std::string::npos);

  std::ifstream sourceFile(sourcePath);
  std::string source((std::istreambuf_iterator<char>(sourceFile)),
                     std::istreambuf_iterator<char>());
  EXPECT_NE(source.find("kGeneratedOperators[]"), std::string::npos);
  EXPECT_NE(source.find("OperatorID::ConstColor,"), std::string::npos);
  EXPECT_NE(source.find("OperatorID::Luma,"), std::string::npos);
  EXPECT_NE(source.find("\"pmApplyLuma\""), std::string::npos);
  EXPECT_NE(source.find("\"pmApplyConstColor\""), std::string::npos);
  EXPECT_NE(source.find("kLumaParams[]"), std::string::npos);
  EXPECT_NE(source.find("uniformData->setData(\"kr\", kr);"), std::string::npos);
  EXPECT_NE(source.find("uniformData->setData(\"constColor\", constColor);"), std::string::npos);
  EXPECT_NE(source.find("const uint32_t kGeneratedOperatorCount = 2;"), std::string::npos);
  EXPECT_NE(source.find("uniformHandler->addUniform(\"kr\", UniformFormat::Float, "
                        "ShaderStage::Fragment);"),
            std::string::npos);
  EXPECT_NE(source.find("const auto inputModeValue = ir->CreateIntConstant(inputMode);"),
            std::string::npos);
  EXPECT_NE(source.find("const auto krValue = ir->InternValue(textNames->kr, SLType::Float, "
                        "Precision::High, IRValueKind::Uniform);"),
            std::string::npos);
  EXPECT_NE(source.find("return ir->CreateCall(OperatorID::Luma, SLType::Float4, "
                        "{color, krValue, kgValue, kbValue});"),
            std::string::npos);
  EXPECT_NE(source.find("return ir->CreateCall(OperatorID::ConstColor, SLType::Float4, "
                        "{constColorValue, inputColor, inputModeValue});"),
            std::string::npos);
}

TEST_F(OperatorCodegenTest, GeneratesAnEmptyTableWithoutAnnotations) {
  WriteInc("helper.inc", "//@function pmHelper\nfloat pmHelper(float v) { return v; }\n");
  std::vector<OperatorSignature> signatures = {};
  std::string error;
  ASSERT_TRUE(CollectOperatorSignatures(root.string(), &signatures, &error)) << error;
  EXPECT_TRUE(signatures.empty());

  const auto headerPath = root / "GeneratedOperators.h";
  const auto sourcePath = root / "GeneratedOperators.cpp";
  const auto idsPath = root / "GeneratedOperatorIDs.h";
  ASSERT_TRUE(WriteGeneratedOperators(signatures, headerPath.string(), sourcePath.string(),
                                      idsPath.string(), &error))
      << error;

  std::ifstream sourceFile(sourcePath);
  std::string source((std::istreambuf_iterator<char>(sourceFile)),
                     std::istreambuf_iterator<char>());
  EXPECT_NE(source.find("const uint32_t kGeneratedOperatorCount = 0;"), std::string::npos);
  EXPECT_NE(source.find("return nullptr;"), std::string::npos);
}

}  // namespace
}  // namespace tgfx

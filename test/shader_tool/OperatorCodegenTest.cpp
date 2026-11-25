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
           "//@function pmAllKinds\n"
           "//@op AllKinds fragment\n"
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

TEST_F(OperatorCodegenTest, DefaultsTheStageAndSkipsUnannotatedFunctions) {
  WriteInc("op_default.inc",
           "//@function pmDefaultStage\n"
           "//@op DefaultStage\n"
           "float pmDefaultStage(float value) { return value; }\n");
  WriteInc("helper.inc", "//@function pmHelper\nfloat pmHelper(float v) { return v; }\n");
  std::vector<OperatorSignature> signatures = {};
  std::string error;
  ASSERT_TRUE(CollectOperatorSignatures(root.string(), &signatures, &error)) << error;
  ASSERT_EQ(signatures.size(), 1u);
  EXPECT_EQ(signatures[0].stage, "fragment");
}

TEST_F(OperatorCodegenTest, RejectsTheAmbiguousForms) {
  const std::string cases[] = {
      "//@function pmA\n//@op A\nvec4 pmA(/*@in*/ float k[4]) { return vec4(0.0); }\n",
      "//@function pmA\n//@op A\nvec4 pmA(Foo bar) { return vec4(0.0); }\n",
      "//@function pmA\n//@op A\nvec4 pmA(float m[3]) { return vec4(0.0); }\n",
      "//@function pmA\n//@op A\nvec4 pmA(float v) { return vec4(v); }\n"
      "//@function pmB\n//@op A\nvec4 pmB(float v) { return vec4(v); }\n",
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

TEST_F(OperatorCodegenTest, RejectsABadStageAndADuplicateAnnotation) {
  WriteInc("op_stage.inc",
           "//@function pmStage\n//@op Stage geometry\nfloat pmStage(float v) { return v; }\n");
  std::vector<OperatorSignature> signatures = {};
  std::string error;
  EXPECT_FALSE(CollectOperatorSignatures(root.string(), &signatures, &error));
  EXPECT_NE(error.find("vertex or fragment"), std::string::npos) << error;

  std::filesystem::remove_all(root / "fn");
  std::filesystem::create_directories(root / "fn");
  WriteInc("op_twice.inc",
           "//@function pmTwice\n//@op Twice\n//@op Twice\nfloat pmTwice(float v) { return v; }\n");
  // The duplicate surfaces as a parse error through the catalog collector, not the operator
  // collector, so the call only has to fail closed.
  EXPECT_FALSE(CollectOperatorSignatures(root.string(), &signatures, &error));
}

TEST_F(OperatorCodegenTest, GeneratesEnumTableAndUniforms) {
  WriteInc("op_luma.inc",
           "//@function pmApplyLuma\n"
           "//@op Luma fragment\n"
           "vec4 pmApplyLuma(/*@in*/ vec4 color, float kr, float kg, float kb) "
           "{ return color; }\n");
  WriteInc("op_kernel.inc",
           "//@function pmKernel\n"
           "//@op Kernel fragment\n"
           "vec4 pmKernel(vec4 slots[17], /*@in*/ int radius) { return slots[0]; }\n");
  std::vector<OperatorSignature> signatures = {};
  std::string error;
  ASSERT_TRUE(CollectOperatorSignatures(root.string(), &signatures, &error)) << error;
  ASSERT_EQ(signatures.size(), 2u);

  const auto headerPath = root / "GeneratedOperators.h";
  const auto sourcePath = root / "GeneratedOperators.cpp";
  ASSERT_TRUE(WriteGeneratedOperators(signatures, headerPath.string(), sourcePath.string(),
                                      &error))
      << error;

  std::ifstream headerFile(headerPath);
  std::string header((std::istreambuf_iterator<char>(headerFile)),
                     std::istreambuf_iterator<char>());
  EXPECT_NE(header.find("enum class GeneratedOperatorID : uint32_t {"), std::string::npos);
  EXPECT_NE(header.find("  Kernel = 1,"), std::string::npos);
  EXPECT_NE(header.find("  Luma = 2,"), std::string::npos);
  EXPECT_NE(header.find("  OpCount,"), std::string::npos);
  EXPECT_NE(header.find("struct LumaUniforms {"), std::string::npos);
  EXPECT_NE(header.find("float kr = {};"), std::string::npos);
  EXPECT_NE(header.find("struct KernelUniforms {"), std::string::npos);
  EXPECT_NE(header.find("std::array<float, 68> slots = {};"), std::string::npos);
  EXPECT_NE(header.find("void upload(UniformData* uniformData) const;"), std::string::npos);

  std::ifstream sourceFile(sourcePath);
  std::string source((std::istreambuf_iterator<char>(sourceFile)),
                     std::istreambuf_iterator<char>());
  EXPECT_NE(source.find("kGeneratedOperators[]"), std::string::npos);
  EXPECT_NE(source.find("\"pmApplyLuma\""), std::string::npos);
  EXPECT_NE(source.find("\"pmKernel\""), std::string::npos);
  EXPECT_NE(source.find("kLumaParams[]"), std::string::npos);
  EXPECT_NE(source.find("uniformData->setData(\"kr\", kr);"), std::string::npos);
  EXPECT_NE(source.find("uniformData->setData(\"slots\", slots);"), std::string::npos);
  EXPECT_NE(source.find("const uint32_t kGeneratedOperatorCount = 2;"), std::string::npos);
}

TEST_F(OperatorCodegenTest, GeneratesAnEmptyTableWithoutAnnotations) {
  WriteInc("helper.inc", "//@function pmHelper\nfloat pmHelper(float v) { return v; }\n");
  std::vector<OperatorSignature> signatures = {};
  std::string error;
  ASSERT_TRUE(CollectOperatorSignatures(root.string(), &signatures, &error)) << error;
  EXPECT_TRUE(signatures.empty());

  const auto headerPath = root / "GeneratedOperators.h";
  const auto sourcePath = root / "GeneratedOperators.cpp";
  ASSERT_TRUE(WriteGeneratedOperators(signatures, headerPath.string(), sourcePath.string(),
                                      &error))
      << error;

  std::ifstream sourceFile(sourcePath);
  std::string source((std::istreambuf_iterator<char>(sourceFile)),
                     std::istreambuf_iterator<char>());
  EXPECT_NE(source.find("const uint32_t kGeneratedOperatorCount = 0;"), std::string::npos);
  EXPECT_NE(source.find("return nullptr;"), std::string::npos);
}

}  // namespace
}  // namespace tgfx

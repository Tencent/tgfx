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

#include <cstdlib>
#include <fstream>
#include "gpu/ir/ProgramIRBuilder.h"
#include "gpu/spirv/SPIRVShaderBuilder.h"
#include "gtest/gtest.h"

namespace tgfx {
namespace {

void WriteIfRequested(const std::vector<uint32_t>& words) {
  const auto* path = std::getenv("TGFX_SPIRV_OUT");
  if (path == nullptr || *path == '\0') {
    return;
  }
  std::ofstream out(path, std::ios::binary);
  out.write(reinterpret_cast<const char*>(words.data()),
            static_cast<std::streamsize>(words.size() * sizeof(uint32_t)));
}

bool ContainsOpcode(const std::vector<uint32_t>& words, uint16_t opcode) {
  for (size_t index = 5; index < words.size();) {
    const auto instruction = words[index];
    const auto wordCount = instruction >> 16;
    if (wordCount == 0) {
      return false;
    }
    if ((instruction & 0xFFFFu) == opcode) {
      return true;
    }
    index += wordCount;
  }
  return false;
}

}  // namespace

TEST(SPIRVShaderBuilderTest, EmitsConstantsConstructAndStore) {
  ProgramIRBuilder program = {};
  const auto zero = program.CreateConstant(0.0f);
  const auto color = program.CreateConstruct(SLType::Float4, {zero, zero, zero, zero});
  const auto output =
      program.CreateValue(SLType::Float4, Precision::High, IRValueKind::Local, "outputColor");
  program.StoreOutput(output, color);

  SPIRVShaderBuilder builder(ShaderStage::Fragment);
  std::string error = {};
  ASSERT_TRUE(builder.Begin(&error)) << error;
  uint32_t valueId = 0;
  ASSERT_TRUE(builder.EmitValue(program.IR(), color, &valueId, &error)) << error;
  EXPECT_NE(valueId, 0u);
  ASSERT_TRUE(builder.StoreOutput(program.IR(), color, &error)) << error;
  const auto words = builder.Finish(&error);
  ASSERT_FALSE(words.empty()) << error;
  WriteIfRequested(words);

  EXPECT_EQ(words[0], 0x07230203u);
  EXPECT_EQ(words[1], 0x00010000u);
  EXPECT_GT(words[3], 5u);
  EXPECT_TRUE(ContainsOpcode(words, spirv::kOpEntryPoint));
  EXPECT_TRUE(ContainsOpcode(words, spirv::kOpFunction));
  EXPECT_TRUE(ContainsOpcode(words, spirv::kOpFunctionEnd));
  EXPECT_TRUE(ContainsOpcode(words, spirv::kOpStore));
}

TEST(SPIRVShaderBuilderTest, EmitsSwizzleAndBinary) {
  ProgramIRBuilder program = {};
  const auto zero = program.CreateConstant(0.0f);
  const auto color = program.CreateConstruct(SLType::Float4, {zero, zero, zero, zero});
  const auto rgb = program.CreateSwizzle(color, SLType::Float3, "rgb");
  const auto doubled = program.CreateBinary(IRBinaryOp::Mul, SLType::Float3, rgb, rgb);
  const auto output = program.CreateConstruct(SLType::Float4, {doubled, zero});

  SPIRVShaderBuilder builder(ShaderStage::Fragment);
  std::string error = {};
  ASSERT_TRUE(builder.Begin(&error)) << error;
  uint32_t valueId = 0;
  ASSERT_TRUE(builder.EmitValue(program.IR(), output, &valueId, &error)) << error;
  const auto words = builder.Finish(&error);
  ASSERT_FALSE(words.empty()) << error;
  EXPECT_TRUE(ContainsOpcode(words, spirv::kOpVectorShuffle));
  EXPECT_TRUE(ContainsOpcode(words, spirv::kOpFMul));
  EXPECT_TRUE(ContainsOpcode(words, spirv::kOpCompositeConstruct));
}

TEST(SPIRVShaderBuilderTest, EmitsBuiltinFragCoord) {
  ProgramIRBuilder program = {};
  const auto fragCoord = program.CreateBuiltin("gl_FragCoord", SLType::Float4);
  const auto xy = program.CreateSwizzle(fragCoord, SLType::Float2, "xy");
  const auto zero = program.CreateConstant(0.0f);
  const auto output = program.CreateConstruct(SLType::Float4, {xy, zero, zero});

  SPIRVShaderBuilder builder(ShaderStage::Fragment);
  std::string error = {};
  ASSERT_TRUE(builder.Begin(&error)) << error;
  uint32_t valueId = 0;
  ASSERT_TRUE(builder.EmitValue(program.IR(), output, &valueId, &error)) << error;
  ASSERT_TRUE(builder.StoreOutput(program.IR(), output, &error)) << error;
  const auto words = builder.Finish(&error);
  ASSERT_FALSE(words.empty()) << error;
  WriteIfRequested(words);

  EXPECT_TRUE(ContainsOpcode(words, spirv::kOpVariable));
  EXPECT_TRUE(ContainsOpcode(words, spirv::kOpLoad));
  EXPECT_TRUE(ContainsOpcode(words, spirv::kOpVectorShuffle));
}

TEST(SPIRVShaderBuilderTest, EmitsLocalLoadAndAssign) {
  ProgramIRBuilder program = {};
  const auto zero = program.CreateConstant(0.0f);
  const auto color = program.CreateConstruct(SLType::Float4, {zero, zero, zero, zero});
  const auto local =
      program.CreateValue(SLType::Float4, Precision::High, IRValueKind::Local, "color");
  const auto load = program.CreateLoad(local, SLType::Float4);
  const auto output =
      program.CreateValue(SLType::Float4, Precision::High, IRValueKind::Local, "outputColor");
  program.StoreOutput(output, load);

  SPIRVShaderBuilder builder(ShaderStage::Fragment);
  std::string error = {};
  ASSERT_TRUE(builder.Begin(&error)) << error;
  ASSERT_TRUE(builder.DeclareLocals(program.IR(), &error)) << error;
  ASSERT_TRUE(builder.Assign(program.IR(), {local, ""}, color, &error)) << error;
  ASSERT_TRUE(builder.StoreOutput(program.IR(), load, &error)) << error;
  const auto words = builder.Finish(&error);
  ASSERT_FALSE(words.empty()) << error;
  WriteIfRequested(words);

  EXPECT_TRUE(ContainsOpcode(words, spirv::kOpVariable));
  EXPECT_TRUE(ContainsOpcode(words, spirv::kOpStore));
  EXPECT_TRUE(ContainsOpcode(words, spirv::kOpLoad));
}

TEST(SPIRVShaderBuilderTest, EmitsTextureSample) {
  ProgramIRBuilder program = {};
  const auto sampler = program.AddSampler({"TextureSampler_0", SLType::Texture2DSampler, 0});
  const auto zero = program.CreateConstant(0.0f);
  const auto coord = program.CreateConstruct(SLType::Float2, {zero, zero});
  const auto sample = program.CreateTextureSample(sampler, coord, SLType::Float4, "textureColor",
                                                  SLType::Texture2DSampler);

  SPIRVShaderBuilder builder(ShaderStage::Fragment);
  std::string error = {};
  ASSERT_TRUE(builder.Begin(&error)) << error;
  uint32_t valueId = 0;
  ASSERT_TRUE(builder.EmitValue(program.IR(), sample, &valueId, &error)) << error;
  ASSERT_TRUE(builder.StoreOutput(program.IR(), sample, &error)) << error;
  const auto words = builder.Finish(&error);
  ASSERT_FALSE(words.empty()) << error;
  WriteIfRequested(words);

  EXPECT_TRUE(ContainsOpcode(words, spirv::kOpImageSampleImplicitLod));
  EXPECT_TRUE(ContainsOpcode(words, spirv::kOpVariable));
  EXPECT_TRUE(ContainsOpcode(words, spirv::kOpDecorate));
}

TEST(SPIRVShaderBuilderTest, EmitsUniformBlockLoad) {
  ProgramIRBuilder program = {};
  const auto uniform =
      program.AddUniform({"Color", SLType::Float4, Precision::High, ShaderStage::Fragment, 1});
  const auto load = program.CreateLoad(uniform, SLType::Float4);
  const auto output =
      program.CreateValue(SLType::Float4, Precision::High, IRValueKind::Local, "outputColor");
  program.StoreOutput(output, load);

  SPIRVShaderBuilder builder(ShaderStage::Fragment);
  std::string error = {};
  ASSERT_TRUE(builder.Begin(&error)) << error;
  uint32_t valueId = 0;
  ASSERT_TRUE(builder.EmitValue(program.IR(), load, &valueId, &error)) << error;
  ASSERT_TRUE(builder.StoreOutput(program.IR(), load, &error)) << error;
  const auto words = builder.Finish(&error);
  ASSERT_FALSE(words.empty()) << error;
  WriteIfRequested(words);

  EXPECT_TRUE(ContainsOpcode(words, spirv::kOpTypeStruct));
  EXPECT_TRUE(ContainsOpcode(words, spirv::kOpMemberDecorate));
  EXPECT_TRUE(ContainsOpcode(words, spirv::kOpAccessChain));
  EXPECT_TRUE(ContainsOpcode(words, spirv::kOpLoad));
}

TEST(SPIRVShaderBuilderTest, EmitsUniformArrayLoad) {
  ProgramIRBuilder program = {};
  const auto kernel = program.AddUniformArray(
      {"Kernel", SLType::Float4, Precision::High, ShaderStage::Fragment, 17});

  SPIRVShaderBuilder builder(ShaderStage::Fragment);
  std::string error = {};
  ASSERT_TRUE(builder.Begin(&error)) << error;
  uint32_t valueId = 0;
  ASSERT_TRUE(builder.EmitValue(program.IR(), kernel, &valueId, &error)) << error;
  const auto words = builder.Finish(&error);
  ASSERT_FALSE(words.empty()) << error;
  WriteIfRequested(words);

  EXPECT_TRUE(ContainsOpcode(words, spirv::kOpTypeArray));
  EXPECT_TRUE(ContainsOpcode(words, spirv::kOpAccessChain));
  EXPECT_TRUE(ContainsOpcode(words, spirv::kOpLoad));
}

TEST(SPIRVShaderBuilderTest, ImportsArrayFunctionParameter) {
  const auto* desc = FindOperator(OperatorID::GaussianBlur1D);
  ASSERT_NE(desc, nullptr);
  OperatorArtifact artifact = {};
  artifact.id = OperatorID::GaussianBlur1D;
  artifact.backend = OperatorBackend::SPIRV;
  artifact.symbol = "gaussianBlur1D";
  artifact.spirv = {0x07230203u};

  SPIRVShaderBuilder builder(ShaderStage::Fragment);
  std::string error = {};
  ASSERT_TRUE(builder.Begin(&error)) << error;
  EXPECT_NE(builder.ImportFunction(*desc, artifact, &error), 0u) << error;
  const auto words = builder.Finish(&error);
  ASSERT_FALSE(words.empty()) << error;
  WriteIfRequested(words);

  EXPECT_TRUE(ContainsOpcode(words, spirv::kOpTypeArray));
  EXPECT_TRUE(ContainsOpcode(words, spirv::kOpFunctionParameter));
}

TEST(SPIRVShaderBuilderTest, EmitsVertexPosition) {
  ProgramIRBuilder program = {};
  const auto zero = program.CreateConstant(0.0f);
  const auto one = program.CreateConstant(1.0f);
  const auto position = program.CreateConstruct(SLType::Float4, {zero, zero, zero, one});
  const auto output =
      program.CreateValue(SLType::Float4, Precision::High, IRValueKind::Local, "gl_Position");
  program.StoreOutput(output, position);

  SPIRVVertexShaderBuilder builder;
  std::string error = {};
  ASSERT_TRUE(builder.Begin(&error)) << error;
  uint32_t valueId = 0;
  ASSERT_TRUE(builder.EmitValue(program.IR(), position, &valueId, &error)) << error;
  ASSERT_TRUE(builder.StoreOutput(program.IR(), position, &error)) << error;
  const auto words = builder.Finish(&error);
  ASSERT_FALSE(words.empty()) << error;
  WriteIfRequested(words);

  EXPECT_TRUE(ContainsOpcode(words, spirv::kOpEntryPoint));
  EXPECT_TRUE(ContainsOpcode(words, spirv::kOpDecorate));
  EXPECT_TRUE(ContainsOpcode(words, spirv::kOpStore));
}

TEST(SPIRVShaderBuilderTest, RejectsUnsupportedExpressions) {
  ProgramIRBuilder program = {};
  const auto input =
      program.CreateValue(SLType::Float4, Precision::High, IRValueKind::Local, "inputColor");
  const auto result =
      program.CreateCall(OperatorID::Luma, SLType::Float4, {input, input, input, input});

  SPIRVShaderBuilder builder(ShaderStage::Fragment);
  std::string error = {};
  ASSERT_TRUE(builder.Begin(&error)) << error;
  uint32_t valueId = 0;
  EXPECT_FALSE(builder.EmitValue(program.IR(), result, &valueId, &error));
  EXPECT_FALSE(error.empty());
}

}  // namespace tgfx

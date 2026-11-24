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

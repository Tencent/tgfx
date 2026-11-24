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
#include "gpu/spirv/SPIRVOperatorEmitter.h"
#include "gtest/gtest.h"

namespace tgfx {
namespace {

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

void WriteIfRequested(const std::vector<uint32_t>& words) {
  const auto* path = std::getenv("TGFX_SPIRV_OUT");
  if (path == nullptr || *path == '\0') {
    return;
  }
  std::ofstream out(path, std::ios::binary);
  out.write(reinterpret_cast<const char*>(words.data()),
            static_cast<std::streamsize>(words.size() * sizeof(uint32_t)));
}

OperatorBundle MakeLumaBundle() {
  OperatorArtifact artifact = {};
  artifact.id = OperatorID::Luma;
  artifact.backend = OperatorBackend::SPIRV;
  artifact.symbol = "pmApplyLuma";
  artifact.spirv = {0x07230203u};
  return OperatorBundle({artifact});
}

}  // namespace

TEST(SPIRVOperatorEmitterTest, EmitsCallToBundleFunction) {
  ProgramIRBuilder program = {};
  const auto zero = program.CreateConstant(0.0f);
  const auto input = program.CreateConstruct(SLType::Float4, {zero, zero, zero, zero});
  const auto kr = program.CreateConstant(0.0f);
  const auto kg = program.CreateConstant(0.0f);
  const auto kb = program.CreateConstant(0.0f);
  const auto result = program.CreateCall(OperatorID::Luma, SLType::Float4, {input, kr, kg, kb});
  const auto output =
      program.CreateValue(SLType::Float4, Precision::High, IRValueKind::Local, "outputColor");
  program.StoreOutput(output, result);

  SPIRVShaderBuilder builder(ShaderStage::Fragment);
  std::string error = {};
  ASSERT_TRUE(builder.Begin(&error)) << error;
  const auto bundle = MakeLumaBundle();
  ASSERT_TRUE(SPIRVOperatorEmitter::EmitCall(program.IR(), result, bundle, &builder, &error))
      << error;
  ASSERT_TRUE(builder.StoreOutput(program.IR(), result, &error)) << error;
  const auto words = builder.Finish(&error);
  ASSERT_FALSE(words.empty()) << error;
  WriteIfRequested(words);

  EXPECT_TRUE(ContainsOpcode(words, spirv::kOpFunctionCall));
  EXPECT_TRUE(ContainsOpcode(words, spirv::kOpDecorate));
  EXPECT_TRUE(ContainsOpcode(words, spirv::kOpFunction));
}

TEST(SPIRVOperatorEmitterTest, RejectsMissingBundleArtifact) {
  ProgramIRBuilder program = {};
  const auto zero = program.CreateConstant(0.0f);
  const auto input = program.CreateConstruct(SLType::Float4, {zero, zero, zero, zero});
  const auto result =
      program.CreateCall(OperatorID::Luma, SLType::Float4, {input, zero, zero, zero});

  SPIRVShaderBuilder builder(ShaderStage::Fragment);
  std::string error = {};
  ASSERT_TRUE(builder.Begin(&error)) << error;
  EXPECT_FALSE(
      SPIRVOperatorEmitter::EmitCall(program.IR(), result, OperatorBundle(), &builder, &error));
  EXPECT_FALSE(error.empty());
}

TEST(SPIRVOperatorEmitterTest, RejectsManifestMismatch) {
  ProgramIRBuilder program = {};
  const auto zero = program.CreateConstant(0.0f);
  const auto input = program.CreateConstruct(SLType::Float4, {zero, zero, zero, zero});
  const auto result = program.CreateCall(OperatorID::Luma, SLType::Float4, {input});

  SPIRVShaderBuilder builder(ShaderStage::Fragment);
  std::string error = {};
  ASSERT_TRUE(builder.Begin(&error)) << error;
  const auto bundle = MakeLumaBundle();
  EXPECT_FALSE(SPIRVOperatorEmitter::EmitCall(program.IR(), result, bundle, &builder, &error));
  EXPECT_FALSE(error.empty());
}

}  // namespace tgfx

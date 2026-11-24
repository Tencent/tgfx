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

#include "gpu/ir/ProgramIRBuilder.h"
#include "gpu/ir/ProgramIRValidation.h"
#include "gtest/gtest.h"

namespace tgfx {

TEST(ProgramIRTest, BuildsTypedCallAndStore) {
  ProgramIRBuilder builder = {};
  const auto input =
      builder.CreateValue(SLType::Float4, Precision::High, IRValueKind::Local, "inputColor");
  const auto kr = builder.CreateConstant(0.299f);
  const auto kg = builder.CreateConstant(0.587f);
  const auto kb = builder.CreateConstant(0.114f);
  ASSERT_NE(input, INVALID_VALUE_ID);
  ASSERT_NE(kr, INVALID_VALUE_ID);
  ASSERT_NE(kg, INVALID_VALUE_ID);
  ASSERT_NE(kb, INVALID_VALUE_ID);

  const auto result = builder.CreateCall(7, SLType::Float4, {input, kr, kg, kb});
  ASSERT_NE(result, INVALID_VALUE_ID);
  builder.StoreOutput(input, result);

  std::string error;
  ASSERT_TRUE(ValidateProgramIR(builder.IR(), &error)) << error;
  ASSERT_EQ(builder.IR().values.size(), 5u);
  ASSERT_EQ(builder.IR().exprs.size(), 4u);
  ASSERT_EQ(builder.IR().statements.size(), 1u);
  const auto& call = builder.IR().exprs.back();
  EXPECT_EQ(call.kind, IRExprKind::Call);
  EXPECT_EQ(call.op, 7u);
  EXPECT_EQ(call.args, (std::vector<ValueID>{input, kr, kg, kb}));
  EXPECT_EQ(builder.IR().statements[0].kind, IRStatementKind::StoreOutput);
}

TEST(ProgramIRTest, KeepsFloatConstantsAsBits) {
  ProgramIRBuilder builder = {};
  const auto value = builder.CreateConstant(0.1f);
  ASSERT_NE(value, INVALID_VALUE_ID);
  ASSERT_EQ(builder.IR().exprs.size(), 1u);
  EXPECT_EQ(builder.IR().exprs[0].constantBits, FloatBits(0.1f));
}

TEST(ProgramIRTest, RecordsFieldAssignment) {
  ProgramIRBuilder builder = {};
  const auto yuv = builder.CreateValue(SLType::Float3, Precision::High, IRValueKind::Local, "yuv");
  const auto sampler =
      builder.CreateValue(SLType::Texture2DSampler, Precision::Default, IRValueKind::Sampler);
  const auto coord = builder.CreateValue(SLType::Float2, Precision::High, IRValueKind::Local);
  const auto sample = builder.CreateTextureSample(sampler, coord, SLType::Float4, "tgfxRead0");
  EXPECT_EQ(builder.IR().values[sample].debugName, "tgfxRead0");
  const auto red = builder.CreateSwizzle(sample, SLType::Float, "r");
  builder.Assign({yuv, "x"}, red);

  std::string error;
  ASSERT_TRUE(ValidateProgramIR(builder.IR(), &error)) << error;
  ASSERT_EQ(builder.IR().statements.size(), 1u);
  EXPECT_EQ(builder.IR().statements[0].dst.base, yuv);
  EXPECT_EQ(builder.IR().statements[0].dst.swizzle, "x");
}

TEST(ProgramIRTest, InternsInterfaceResources) {
  ProgramIRBuilder builder = {};
  const auto attributeValue =
      builder.InternValue("position", SLType::Float2, Precision::High, IRValueKind::Attribute);
  const auto attributeId = builder.AddAttribute({"position", SLType::Float2, Precision::High, 0});
  EXPECT_EQ(attributeId, attributeValue);
  EXPECT_EQ(builder.Interface().attributes.size(), 1u);

  const auto uniformValue =
      builder.InternValue("color", SLType::Float4, Precision::High, IRValueKind::Uniform);
  const auto uniformId =
      builder.AddUniform({"color", SLType::Float4, Precision::High, ShaderStage::Fragment, 1});
  EXPECT_EQ(uniformId, uniformValue);
  EXPECT_EQ(builder.Interface().uniforms.size(), 1u);

  const auto samplerId = builder.AddSampler({"sampler", SLType::Texture2DSampler, 0});
  ASSERT_NE(samplerId, INVALID_VALUE_ID);
  EXPECT_EQ(builder.Interface().samplers.size(), 1u);

  const auto varyingId = builder.AddVarying({"vColor", SLType::Float4, Precision::High, 0, false});
  ASSERT_NE(varyingId, INVALID_VALUE_ID);
  EXPECT_EQ(builder.Interface().varyings.size(), 1u);
  EXPECT_EQ(builder.Interface().attributes[0].location, 0u);
  EXPECT_EQ(builder.Interface().varyings[0].location, 0u);
  EXPECT_EQ(builder.Interface().samplers[0].binding, 0u);

  std::string error;
  EXPECT_TRUE(ValidateProgramIR(builder.IR(), &error)) << error;
}

TEST(ProgramIRTest, LaysOutUniformsWithStd140) {
  ProgramIRBuilder builder = {};
  builder.AddUniform({"u0", SLType::Float4, Precision::High, ShaderStage::Fragment, 1});
  builder.AddUniform({"u1", SLType::Float, Precision::High, ShaderStage::Fragment, 1});
  builder.AddUniform({"u2", SLType::Float4, Precision::High, ShaderStage::Fragment, 1});
  builder.AddUniform({"u3", SLType::Float3, Precision::High, ShaderStage::Fragment, 1});
  builder.AddUniform({"u4", SLType::Float4, Precision::High, ShaderStage::Fragment, 2});

  const auto& uniforms = builder.Interface().uniforms;
  ASSERT_EQ(uniforms.size(), 5u);
  EXPECT_EQ(uniforms[0].offset, 0u);
  EXPECT_EQ(uniforms[1].offset, 16u);
  EXPECT_EQ(uniforms[2].offset, 32u);
  EXPECT_EQ(uniforms[3].offset, 48u);
  EXPECT_EQ(uniforms[3].size, 12u);
  EXPECT_EQ(uniforms[4].offset, 64u);
  EXPECT_EQ(uniforms[4].arrayStride, 16u);
  EXPECT_EQ(uniforms[4].size, 32u);
  ASSERT_EQ(builder.Interface().uniformBlocks.size(), 1u);
  EXPECT_EQ(builder.Interface().uniformBlocks[0].size, 96u);
  std::string error;
  EXPECT_TRUE(ValidateProgramIR(builder.IR(), &error)) << error;
}

TEST(ProgramIRTest, RejectsUniformBlockBindingMismatch) {
  ProgramIR program = {};
  UniformBlockIR block = {};
  block.stage = ShaderStage::Fragment;
  block.binding = 1;
  block.size = 16;
  program.interface.uniformBlocks.push_back(block);

  ResourceBinding binding = {};
  binding.kind = BindingKind::UniformBuffer;
  binding.stage = ShaderStage::Fragment;
  binding.binding = 0;
  program.interface.bindings.push_back(binding);

  std::string error;
  EXPECT_FALSE(ValidateProgramIR(program, &error));
  EXPECT_NE(error.find("binding"), std::string::npos);
}

TEST(ProgramIRTest, RejectsDuplicateSamplerBindings) {
  ProgramIR program = {};
  ResourceBinding first = {};
  first.kind = BindingKind::Sampler;
  first.binding = 3;
  first.stage = ShaderStage::Fragment;
  program.interface.bindings = {first, first};

  std::string error;
  EXPECT_FALSE(ValidateProgramIR(program, &error));
  EXPECT_NE(error.find("duplicate"), std::string::npos);
}

TEST(ProgramIRTest, RejectsForwardReferences) {
  ProgramIR program = {};
  IRValue first = {};
  first.id = 0;
  first.type = SLType::Float4;
  first.kind = IRValueKind::Expr;
  first.definingExpr = 0;
  IRValue second = {};
  second.id = 1;
  second.type = SLType::Float4;
  second.kind = IRValueKind::Local;
  program.values = {first, second};

  IRExpr expr = {};
  expr.id = 0;
  expr.kind = IRExprKind::Call;
  expr.result = 0;
  expr.args = {1};
  expr.type = SLType::Float4;
  expr.op = 1;
  program.exprs = {expr};

  std::string error;
  EXPECT_FALSE(ValidateProgramIR(program, &error));
  EXPECT_NE(error.find("later value"), std::string::npos);
}

TEST(ProgramIRTest, RejectsResourceWithDefinition) {
  ProgramIR program = {};
  IRValue value = {};
  value.id = 0;
  value.type = SLType::Float4;
  value.kind = IRValueKind::Uniform;
  value.definingExpr = 0;
  program.values = {value};

  std::string error;
  EXPECT_FALSE(ValidateProgramIR(program, &error));
  EXPECT_NE(error.find("cannot have a defining expression"), std::string::npos);
}

}  // namespace tgfx

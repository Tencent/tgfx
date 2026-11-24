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

#include "gpu/glsl/GLSLOperatorEmitter.h"
#include "gpu/ir/ProgramIRBuilder.h"
#include "gtest/gtest.h"

namespace tgfx {

TEST(GLSLOperatorEmitterTest, EmitsLumaCall) {
  ProgramIRBuilder builder = {};
  const auto input =
      builder.InternValue("inputColor", SLType::Float4, Precision::High, IRValueKind::Local);
  const auto kr = builder.InternValue("kr", SLType::Float, Precision::High, IRValueKind::Uniform);
  const auto kg = builder.InternValue("kg", SLType::Float, Precision::High, IRValueKind::Uniform);
  const auto kb = builder.InternValue("kb", SLType::Float, Precision::High, IRValueKind::Uniform);
  const auto result = builder.CreateCall(OperatorID::Luma, SLType::Float4, {input, kr, kg, kb});

  std::string expression = {};
  std::string error = {};
  ASSERT_TRUE(GLSLOperatorEmitter::EmitCall(builder.IR(), result, &expression, &error)) << error;
  EXPECT_EQ(expression, "pmApplyLuma(inputColor, kr, kg, kb)");
}

TEST(GLSLOperatorEmitterTest, FormatsFloatConstants) {
  ProgramIRBuilder builder = {};
  const auto input =
      builder.InternValue("inputColor", SLType::Float4, Precision::High, IRValueKind::Local);
  const auto zero = builder.CreateConstant(0.0f);
  const auto result = builder.CreateCall(OperatorID::AlphaThreshold, SLType::Float4, {input, zero});

  std::string expression = {};
  std::string error = {};
  ASSERT_TRUE(GLSLOperatorEmitter::EmitCall(builder.IR(), result, &expression, &error)) << error;
  EXPECT_EQ(expression, "pmApplyAlphaThreshold(inputColor, 0.0)");
}

TEST(GLSLOperatorEmitterTest, EmitsUniformArrayArgument) {
  ProgramIRBuilder builder = {};
  const auto sampler = builder.InternValue("sampler", SLType::Texture2DSampler, Precision::Default,
                                           IRValueKind::Sampler);
  const auto coord =
      builder.InternValue("coord", SLType::Float2, Precision::High, IRValueKind::Local);
  const auto texel =
      builder.InternValue("texel", SLType::Float2, Precision::High, IRValueKind::Uniform);
  const auto radius =
      builder.InternValue("radius", SLType::Int, Precision::High, IRValueKind::Uniform);
  const auto subset =
      builder.InternValue("subset", SLType::Float4, Precision::High, IRValueKind::Uniform);
  const auto clamp =
      builder.InternValue("clamp", SLType::Float4, Precision::High, IRValueKind::Uniform);
  const auto dimension =
      builder.InternValue("dimension", SLType::Float2, Precision::High, IRValueKind::Uniform);
  const auto kernel =
      builder.InternValue("Kernel", SLType::Float4, Precision::High, IRValueKind::UniformArray);
  const auto modeX = builder.CreateIntConstant(1);
  const auto modeY = builder.CreateIntConstant(0);
  const auto seam = builder.CreateIntConstant(0);
  const auto border = builder.CreateIntConstant(0);
  const auto result = builder.CreateCall(OperatorID::GaussianBlur1D, SLType::Float4,
                                         {sampler, coord, texel, radius, subset, modeX, modeY,
                                          clamp, dimension, seam, border, kernel});

  std::string expression = {};
  std::string error = {};
  ASSERT_TRUE(GLSLOperatorEmitter::EmitCall(builder.IR(), result, &expression, &error)) << error;
  EXPECT_EQ(expression,
            "gaussianBlur1D(sampler, coord, texel, radius, subset, 1, 0, clamp, dimension, 0, 0, "
            "Kernel)");
}

TEST(GLSLOperatorEmitterTest, RejectsNonCallValues) {
  ProgramIRBuilder builder = {};
  const auto value =
      builder.CreateValue(SLType::Float4, Precision::High, IRValueKind::Local, "local");
  std::string expression = {};
  std::string error = {};
  EXPECT_FALSE(GLSLOperatorEmitter::EmitCall(builder.IR(), value, &expression, &error));
  EXPECT_FALSE(error.empty());
}

}  // namespace tgfx

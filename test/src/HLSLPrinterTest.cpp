////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License") you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
////////////////////////////////////////////////////////////////////////////////////////////////

#include "base/TGFXTest.h"
#include "gpu/hlsl/HLSLOperatorEmitter.h"
#include "gpu/hlsl/HLSLPrinter.h"
#include "gpu/ir/ProgramIRBuilder.h"
#include "gtest/gtest.h"

namespace tgfx {

// The HLSL spelling of every IR type: the printer declares locals from this table, so a missing
// entry silently drops a declaration and the shader stops compiling.
TGFX_TEST(HLSLPrinterTest, EveryScalarAndMatrixTypeHasAnHLSLSpelling) {
  const SLType types[] = {SLType::Float,    SLType::Float2,   SLType::Float3,   SLType::Float4,
                          SLType::Int,      SLType::Int2,     SLType::Int3,     SLType::Int4,
                          SLType::Float2x2, SLType::Float3x3, SLType::Float4x4};
  for (const auto type : types) {
    EXPECT_NE(HLSLOperatorEmitter::TypeName(type), nullptr)
        << static_cast<int>(type) << " has no HLSL spelling";
  }
  EXPECT_STREQ(HLSLOperatorEmitter::TypeName(SLType::Float4), "float4");
  EXPECT_STREQ(HLSLOperatorEmitter::TypeName(SLType::Int4), "int4");
  EXPECT_STREQ(HLSLOperatorEmitter::TypeName(SLType::Float3x3), "float3x3");
  // A sampler is a resource, not a value the body can declare.
  EXPECT_EQ(HLSLOperatorEmitter::TypeName(SLType::Texture2DSampler), nullptr);
}

// The body prints HLSL constructs, never the GLSL spellings the IR also carries.
TGFX_TEST(HLSLPrinterTest, BodiesPrintHlslConstructs) {
  ProgramIRBuilder builder = {};
  const auto one = builder.CreateConstant(1.0f);
  const auto zero = builder.CreateConstant(0.0f);
  const auto color = builder.CreateConstruct(SLType::Float4, {one, zero, zero, one});
  const auto target = builder.CreateValue(SLType::Float4, Precision::High, IRValueKind::Local,
                                          "fragColor");
  builder.Assign({target, ""}, color);
  const auto text = HLSLPrinter::PrintFragmentBodyFromIR(builder.IR());
  ASSERT_FALSE(text.empty());
  EXPECT_NE(text.find("float4 fragColor = float4(1.0, 0.0, 0.0, 1.0);"), std::string::npos)
      << text;
  EXPECT_EQ(text.find("vec4"), std::string::npos) << text;
}

// A texture read goes through the shared reader, which Direct3D declares as a texture/sampler
// pair: the call site passes both, then the coordinate.
TGFX_TEST(HLSLPrinterTest, TextureReadsPassTheSamplerPair) {
  ProgramIRBuilder builder = {};
  builder.AddSampler({"TextureSampler_0", SLType::Texture2DSampler, 0});
  const auto sampler = builder.InternValue("TextureSampler_0", SLType::Texture2DSampler,
                                           Precision::High, IRValueKind::Sampler);
  const auto zero = builder.CreateConstant(0.0f);
  const auto coord = builder.CreateConstruct(SLType::Float2, {zero, zero});
  const auto sample = builder.CreateTextureSample(sampler, coord, SLType::Float4, "texel",
                                                  SLType::Texture2DSampler);
  const auto target =
      builder.CreateValue(SLType::Float4, Precision::High, IRValueKind::Local, "sampledColor");
  builder.Assign({target, ""}, sample);
  const auto text = HLSLPrinter::PrintFragmentBodyFromIR(builder.IR());
  ASSERT_FALSE(text.empty());
  EXPECT_NE(text.find("tgfxTextureSample(TextureSampler_0, TextureSampler_0_sampler,"),
            std::string::npos)
      << text;
}

// RawText carries GLSL text from a processor without an IR form. There is no HLSL spelling of it,
// so the printer refuses the program instead of compiling GLSL into an HLSL shader.
TGFX_TEST(HLSLPrinterTest, RawTextIsRefused) {
  ProgramIRBuilder builder = {};
  builder.AppendRawText("  vec4 raw = vec4(1.0);\n", "raw");
  EXPECT_TRUE(HLSLPrinter::PrintFragmentBodyFromIR(builder.IR()).empty());
}

// The vertex body writes the position through a local the epilogue copies into SV_Position, so the
// printed text must name that local rather than gl_Position.
TGFX_TEST(HLSLPrinterTest, VertexPositionUsesTheDirect3DLocal) {
  ProgramIRBuilder builder = {};
  const auto zero = builder.CreateConstant(0.0f);
  const auto one = builder.CreateConstant(1.0f);
  const auto position4 = builder.CreateConstruct(SLType::Float4, {zero, zero, zero, one});
  const auto output = builder.CreateValue(SLType::Float4, Precision::High, IRValueKind::Local,
                                          "gl_Position");
  builder.StoreOutput(output, position4);
  const auto text = HLSLPrinter::PrintVertexBodyFromIR(builder.IR());
  ASSERT_FALSE(text.empty()) << "the vertex body refused a store to gl_Position";
  EXPECT_NE(text.find("tgfx_Position = "), std::string::npos) << text;
  EXPECT_EQ(text.find("gl_Position"), std::string::npos) << text;
}

}  // namespace tgfx

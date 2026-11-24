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
//  license is distributed on an "AS IS" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include <cstdlib>
#include <fstream>
#include "gpu/ir/ProgramIRBuilder.h"
#include "gpu/spirv/SPIRVProgramBuilder.h"
#include "gtest/gtest.h"

namespace tgfx {
namespace {

void WriteIfRequested(const char* env, const std::vector<uint32_t>& words) {
  const auto* path = std::getenv(env);
  if (path == nullptr || *path == '\0') {
    return;
  }
  std::ofstream out(path, std::ios::binary);
  out.write(reinterpret_cast<const char*>(words.data()),
            static_cast<std::streamsize>(words.size() * sizeof(uint32_t)));
}

}  // namespace

TEST(SPIRVProgramBuilderTest, BuildsVertexAndFragmentModules) {
  ProgramIRBuilder vertex = {};
  const auto attribute = vertex.AddAttribute({"aPosition", SLType::Float2, Precision::High, 0});
  const auto varying = vertex.AddVarying({"vColor", SLType::Float4, Precision::High, 0});
  const auto position = vertex.CreateLoad(attribute, SLType::Float2);
  const auto zero = vertex.CreateConstant(0.0f);
  const auto one = vertex.CreateConstant(1.0f);
  const auto clipPosition = vertex.CreateConstruct(SLType::Float4, {position, zero, one});
  const auto red = vertex.CreateConstruct(SLType::Float4, {one, zero, zero, one});
  const auto positionOutput =
      vertex.CreateValue(SLType::Float4, Precision::High, IRValueKind::Local, "gl_Position");
  vertex.StoreOutput(positionOutput, clipPosition);
  vertex.StoreVarying({varying, ""}, red);

  ProgramIRBuilder fragment = {};
  const auto fragmentVarying = fragment.AddVarying({"vColor", SLType::Float4, Precision::High, 0});
  const auto colorOutput =
      fragment.CreateValue(SLType::Float4, Precision::High, IRValueKind::Local, "outputColor");
  fragment.StoreOutput(colorOutput, fragmentVarying);

  SPIRVProgramModules modules = {};
  std::string error = {};
  ASSERT_TRUE(SPIRVProgramBuilder::Build(vertex.IR(), fragment.IR(), &modules, &error)) << error;
  EXPECT_FALSE(modules.vertex.empty());
  EXPECT_FALSE(modules.fragment.empty());
  EXPECT_EQ(modules.vertex[0], 0x07230203u);
  EXPECT_EQ(modules.fragment[0], 0x07230203u);
  EXPECT_TRUE(modules.layout.bindings().empty());
  WriteIfRequested("TGFX_SPIRV_VERTEX_OUT", modules.vertex);
  WriteIfRequested("TGFX_SPIRV_FRAGMENT_OUT", modules.fragment);
}

TEST(SPIRVProgramBuilderTest, BuildsFromOneStagedProgram) {
  ProgramIRBuilder program = {};
  program.SetStage(ShaderStage::Vertex);
  const auto attribute = program.AddAttribute({"aPosition", SLType::Float2, Precision::High, 0});
  const auto varying = program.AddVarying({"vColor", SLType::Float4, Precision::High, 0});
  const auto position = program.CreateLoad(attribute, SLType::Float2);
  const auto zero = program.CreateConstant(0.0f);
  const auto one = program.CreateConstant(1.0f);
  const auto clipPosition = program.CreateConstruct(SLType::Float4, {position, zero, one});
  const auto red = program.CreateConstruct(SLType::Float4, {one, zero, zero, one});
  const auto positionOutput =
      program.CreateValue(SLType::Float4, Precision::High, IRValueKind::Local, "gl_Position");
  program.StoreOutput(positionOutput, clipPosition);
  program.StoreVarying({varying, ""}, red);

  program.SetStage(ShaderStage::Fragment);
  const auto colorOutput =
      program.CreateValue(SLType::Float4, Precision::High, IRValueKind::Local, "outputColor");
  program.StoreOutput(colorOutput, varying);

  SPIRVProgramModules modules = {};
  std::string error = {};
  ASSERT_TRUE(SPIRVProgramBuilder::Build(program.IR(), &modules, &error)) << error;
  EXPECT_FALSE(modules.vertex.empty());
  EXPECT_FALSE(modules.fragment.empty());
  WriteIfRequested("TGFX_SPIRV_VERTEX_OUT", modules.vertex);
  WriteIfRequested("TGFX_SPIRV_FRAGMENT_OUT", modules.fragment);
}

}  // namespace tgfx

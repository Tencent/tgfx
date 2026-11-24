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
#include "gpu/spirv/SPIRVInterfaceLayout.h"
#include "gtest/gtest.h"

namespace tgfx {

TEST(SPIRVInterfaceLayoutTest, MapsUniformsAndSamplersToDescriptorSets) {
  ProgramIRBuilder program = {};
  program.AddUniform({"ViewMatrix", SLType::Float4x4, Precision::High, ShaderStage::Vertex, 1});
  program.AddUniform({"Color", SLType::Float4, Precision::High, ShaderStage::Fragment, 1});
  program.AddSampler({"TextureSampler_0", SLType::Texture2DSampler, 0});

  SPIRVInterfaceLayout layout = {};
  std::string error = {};
  ASSERT_TRUE(layout.Build(program.IR().interface, &error)) << error;
  ASSERT_EQ(layout.bindings().size(), 3u);

  EXPECT_EQ(layout.bindings()[0].set, 0u);
  EXPECT_EQ(layout.bindings()[0].binding, 0u);
  EXPECT_EQ(layout.bindings()[0].stage, ShaderStage::Vertex);

  EXPECT_EQ(layout.bindings()[1].set, 1u);
  EXPECT_EQ(layout.bindings()[1].binding, 1u);
  EXPECT_EQ(layout.bindings()[1].stage, ShaderStage::Fragment);

  EXPECT_EQ(layout.bindings()[2].set, 2u);
  EXPECT_EQ(layout.bindings()[2].binding, 0u);
  EXPECT_EQ(layout.bindings()[2].type, SLType::Texture2DSampler);
}

TEST(SPIRVInterfaceLayoutTest, RejectsDuplicateBindings) {
  InterfaceIR interface = {};
  interface.uniformBlocks.push_back({ShaderStage::Fragment, 1, 64});
  interface.uniformBlocks.push_back({ShaderStage::Fragment, 1, 64});

  SPIRVInterfaceLayout layout = {};
  std::string error = {};
  EXPECT_FALSE(layout.Build(interface, &error));
  EXPECT_FALSE(error.empty());
}

}  // namespace tgfx

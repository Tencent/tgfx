/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include <array>
#include <cstring>
#include <vector>
#include "gpu/UniformData.h"
#include "gpu/ir/ProgramIRBuilder.h"
#include "gtest/gtest.h"

namespace tgfx {

TEST(UniformDataTest, EmptyInterfaceHasNoBlock) {
  const InterfaceIR interface = {};
  EXPECT_EQ(UniformData::Make(interface, ShaderStage::Vertex), nullptr);
  EXPECT_EQ(UniformData::Make(interface, ShaderStage::Fragment), nullptr);
}

TEST(UniformDataTest, CopiesTheIROffsetsAndSizes) {
  ProgramIRBuilder builder = {};
  ASSERT_NE(builder.AddUniform({"scalar", SLType::Float, Precision::Default, ShaderStage::Fragment, 1}),
            INVALID_VALUE_ID);
  ASSERT_NE(
      builder.AddUniform({"vector3", SLType::Float3, Precision::Default, ShaderStage::Fragment, 1}),
      INVALID_VALUE_ID);
  ASSERT_NE(
      builder.AddUniform({"vector4", SLType::Float4, Precision::Default, ShaderStage::Fragment, 1}),
      INVALID_VALUE_ID);
  ASSERT_NE(builder.AddUniform({"kernel", SLType::Float4, Precision::Default, ShaderStage::Fragment, 2}),
            INVALID_VALUE_ID);

  auto data = UniformData::Make(builder.IR().interface, ShaderStage::Fragment);
  ASSERT_NE(data, nullptr);
  EXPECT_EQ(data->size(), 80u);
  // The fragment stage carries no vertex block.
  EXPECT_EQ(UniformData::Make(builder.IR().interface, ShaderStage::Vertex), nullptr);

  std::vector<uint8_t> buffer(data->size(), 0);
  data->setBuffer(buffer.data());

  const float scalar = 1.5f;
  const std::array<float, 3> vector3 = {1.0f, 2.0f, 3.0f};
  const std::array<float, 8> kernel = {0.0f, 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f};
  data->setData("scalar", scalar);
  data->setData("vector3", vector3);
  data->setData("kernel", kernel);

  EXPECT_EQ(std::memcmp(buffer.data() + 0, &scalar, sizeof(scalar)), 0);
  EXPECT_EQ(std::memcmp(buffer.data() + 16, vector3.data(), sizeof(float) * 3), 0);
  EXPECT_EQ(std::memcmp(buffer.data() + 48, kernel.data(), sizeof(float) * 8), 0);
}

TEST(UniformDataTest, RefusesTwoBlocksInOneStage) {
  InterfaceIR interface = {};
  interface.uniformBlocks.push_back({ShaderStage::Fragment, 0, 16, "FragmentUniformBlock"});
  interface.uniformBlocks.push_back({ShaderStage::Fragment, 1, 16, "FragmentExtraBlock"});
  EXPECT_EQ(UniformData::Make(interface, ShaderStage::Fragment), nullptr);
}

}  // namespace tgfx

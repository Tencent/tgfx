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

#include "gpu/UniformLayout.h"
#include "gpu/ir/ProgramIRBuilder.h"
#include "gtest/gtest.h"

namespace tgfx {
namespace {

// The expected rows, spelled out rather than read back from the table: the table is a contract,
// so the test pins the numbers independently and fails when either side changes alone.
struct Expectation {
  UniformFormat format;
  SLType type;
  uint32_t size;
  uint32_t alignment;
  uint32_t stride;
};

constexpr Expectation kExpected[] = {
    {UniformFormat::Float, SLType::Float, 4, 4, 16},
    {UniformFormat::Float2, SLType::Float2, 8, 8, 16},
    {UniformFormat::Float3, SLType::Float3, 12, 16, 16},
    {UniformFormat::Float4, SLType::Float4, 16, 16, 16},
    {UniformFormat::Float2x2, SLType::Float2x2, 32, 16, 32},
    {UniformFormat::Float3x3, SLType::Float3x3, 48, 16, 48},
    {UniformFormat::Float4x4, SLType::Float4x4, 64, 16, 64},
    {UniformFormat::Int, SLType::Int, 4, 4, 16},
    {UniformFormat::Int2, SLType::Int2, 8, 8, 16},
    {UniformFormat::Int3, SLType::Int3, 12, 16, 16},
    {UniformFormat::Int4, SLType::Int4, 16, 16, 16},
    {UniformFormat::Texture2DSampler, SLType::Texture2DSampler, 0, 0, 0},
    {UniformFormat::TextureExternalSampler, SLType::TextureExternalSampler, 0, 0, 0},
    {UniformFormat::Texture2DRectSampler, SLType::Texture2DRectSampler, 0, 0, 0},
};

}  // namespace

TEST(UniformLayoutTest, MatchesTheStd140Table) {
  for (const auto& expected : kExpected) {
    const auto layout = Std140LayoutOf(expected.format);
    const auto label = static_cast<int>(expected.format);
    EXPECT_EQ(layout.size, expected.size) << label;
    EXPECT_EQ(layout.alignment, expected.alignment) << label;
    EXPECT_EQ(layout.arrayStride, expected.stride) << label;
  }
}

TEST(UniformLayoutTest, TheFormatAndTypeViewsAgree) {
  for (const auto& expected : kExpected) {
    const auto label = static_cast<int>(expected.format);
    EXPECT_EQ(SLTypeOf(expected.format), expected.type) << label;
    const auto byFormat = Std140LayoutOf(expected.format);
    const auto byType = Std140LayoutOf(expected.type);
    EXPECT_EQ(byType.size, byFormat.size) << label;
    EXPECT_EQ(byType.alignment, byFormat.alignment) << label;
    EXPECT_EQ(byType.arrayStride, byFormat.arrayStride) << label;
  }
}

TEST(UniformLayoutTest, ScalarAndVectorArraysStrideSixteenBytes) {
  EXPECT_EQ(Std140LayoutOf(SLType::Float).arrayStride, 16u);
  EXPECT_EQ(Std140LayoutOf(SLType::Float2).arrayStride, 16u);
  EXPECT_EQ(Std140LayoutOf(SLType::Float3).arrayStride, 16u);
  EXPECT_EQ(Std140LayoutOf(SLType::Float4).arrayStride, 16u);
  EXPECT_EQ(Std140LayoutOf(SLType::Int3).arrayStride, 16u);
  EXPECT_EQ(Std140LayoutOf(SLType::Float3x3).arrayStride, 48u);
  EXPECT_EQ(Std140LayoutOf(SLType::Float4x4).arrayStride, 64u);
}

TEST(UniformLayoutTest, AlignsBlockOffsets) {
  EXPECT_EQ(Std140AlignUp(0, 16), 0u);
  EXPECT_EQ(Std140AlignUp(1, 16), 16u);
  EXPECT_EQ(Std140AlignUp(16, 16), 16u);
  EXPECT_EQ(Std140AlignUp(17, 16), 32u);
  EXPECT_EQ(Std140AlignUp(17, 4), 20u);
  EXPECT_EQ(Std140AlignUp(7, 0), 7u);
}

TEST(UniformLayoutTest, TheIRPlacesMembersWithTheSameTable) {
  ProgramIRBuilder builder = {};
  const auto add = [&builder](const char* name, SLType type, uint32_t count) {
    return builder.AddUniform({name, type, Precision::Default, ShaderStage::Fragment, count});
  };
  ASSERT_NE(add("scalar", SLType::Float, 1), INVALID_VALUE_ID);
  ASSERT_NE(add("vector3", SLType::Float3, 1), INVALID_VALUE_ID);
  ASSERT_NE(add("vector4", SLType::Float4, 1), INVALID_VALUE_ID);
  ASSERT_NE(add("kernel", SLType::Float4, 2), INVALID_VALUE_ID);

  const auto& uniforms = builder.IR().interface.uniforms;
  ASSERT_EQ(uniforms.size(), 4u);
  EXPECT_EQ(uniforms[0].offset, 0u);
  EXPECT_EQ(uniforms[0].size, 4u);
  EXPECT_EQ(uniforms[1].offset, 16u);
  EXPECT_EQ(uniforms[1].size, 12u);
  EXPECT_EQ(uniforms[2].offset, 32u);
  EXPECT_EQ(uniforms[2].size, 16u);
  EXPECT_EQ(uniforms[3].offset, 48u);
  EXPECT_EQ(uniforms[3].size, 32u);
  EXPECT_EQ(uniforms[3].arrayStride, 16u);

  const auto& blocks = builder.IR().interface.uniformBlocks;
  ASSERT_EQ(blocks.size(), 1u);
  EXPECT_EQ(blocks[0].size, 80u);
}

}  // namespace tgfx

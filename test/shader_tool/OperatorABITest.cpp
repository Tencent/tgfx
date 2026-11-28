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

#include <vector>
#include "compile/SPIRVLibrary.h"
#include "gtest/gtest.h"

namespace tgfx {

TEST(OperatorABITest, MapsTheDeclarationVocabulary) {
  EXPECT_EQ(AbiOf(SLType::Float), OperatorABIType::Float);
  EXPECT_EQ(AbiOf(SLType::Int), OperatorABIType::Int);
  EXPECT_EQ(AbiOf(SLType::Float2), OperatorABIType::Vec2);
  EXPECT_EQ(AbiOf(SLType::Float3), OperatorABIType::Vec3);
  EXPECT_EQ(AbiOf(SLType::Float4), OperatorABIType::Vec4);
  EXPECT_EQ(AbiOf(SLType::Float3x3), OperatorABIType::Mat3);
  EXPECT_EQ(AbiOf(SLType::Float4x4), OperatorABIType::Mat4);
  EXPECT_EQ(AbiOf(SLType::Int4), OperatorABIType::IVec4);
  EXPECT_EQ(AbiOf(SLType::Texture2DSampler), OperatorABIType::SampledImage);
  EXPECT_EQ(AbiOf(SLType::Texture2DRectSampler), OperatorABIType::SampledImage);
  EXPECT_EQ(AbiOf(SLType::TextureExternalSampler), OperatorABIType::SampledImage);
}

TEST(OperatorABITest, RejectsTypesWithoutAnAbiSpelling) {
  EXPECT_EQ(AbiOf(SLType::Half), OperatorABIType::Unknown);
  EXPECT_EQ(AbiOf(SLType::Int2), OperatorABIType::Unknown);
  EXPECT_EQ(AbiOf(SLType::Int3), OperatorABIType::Unknown);
  EXPECT_EQ(AbiOf(SLType::Float2x2), OperatorABIType::Unknown);
}

TEST(OperatorABITest, SpellsAnArrayParameterAsItsStridedForm) {
  EXPECT_EQ(AbiOf({SLType::Float4, true, 8}), OperatorABIType::Vec4Array8);
  EXPECT_EQ(AbiOf({SLType::Float4, true, 4}), OperatorABIType::Unknown);
  EXPECT_EQ(AbiOf({SLType::Float2, true, 8}), OperatorABIType::Unknown);
}

TEST(OperatorABITest, MatchesOnlyWhenEveryParameterAgrees) {
  const std::vector<OperatorModuleParam> declared = {{SLType::Float4, false, 0},
                                                     {SLType::Float, false, 0},
                                                     {SLType::Float, false, 0},
                                                     {SLType::Float, false, 0}};
  EXPECT_TRUE(MatchesDeclaredAbi(declared, {OperatorABIType::Vec4, OperatorABIType::Float,
                                            OperatorABIType::Float, OperatorABIType::Float}));
  // A shorter or longer module signature is a mismatch even when the common prefix agrees.
  EXPECT_FALSE(MatchesDeclaredAbi(declared, {OperatorABIType::Vec4, OperatorABIType::Float,
                                             OperatorABIType::Float}));
  // The failure the wrapper compile cannot see: a parameter lowered to another scalar type.
  EXPECT_FALSE(MatchesDeclaredAbi(declared, {OperatorABIType::Vec4, OperatorABIType::Float,
                                             OperatorABIType::Int, OperatorABIType::Float}));
  EXPECT_FALSE(MatchesDeclaredAbi({}, {OperatorABIType::Float}));
  EXPECT_TRUE(MatchesDeclaredAbi({}, {}));
}

TEST(OperatorABITest, DescribesBothSidesForDiagnostics) {
  const std::vector<OperatorModuleParam> declared = {{SLType::Float4, false, 0},
                                                     {SLType::Texture2DSampler, false, 0}};
  EXPECT_EQ(DescribeAbi(declared), "vec4, sampler");
  EXPECT_EQ(DescribeAbi(std::vector<OperatorABIType>{OperatorABIType::Vec4,
                                                     OperatorABIType::SampledImage}),
            "vec4, sampler");
}

}  // namespace tgfx

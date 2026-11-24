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

#include "gpu/ir/OperatorManifest.h"
#include "gtest/gtest.h"

namespace tgfx {

TEST(OperatorManifestTest, ExposesLumaSignature) {
  const auto* desc = FindOperator(OperatorID::Luma);
  ASSERT_NE(desc, nullptr);
  EXPECT_STREQ(desc->debugName, "Luma");
  EXPECT_STREQ(desc->glslSymbol, "pmApplyLuma");
  EXPECT_EQ(desc->returnType, SLType::Float4);
  ASSERT_EQ(desc->params.size(), 4u);
  EXPECT_EQ(desc->params[0], SLType::Float4);
  EXPECT_EQ(desc->params[1], SLType::Float);
  EXPECT_EQ(desc->params[2], SLType::Float);
  EXPECT_EQ(desc->params[3], SLType::Float);
}

TEST(OperatorManifestTest, ExposesDeviceCoordSignature) {
  const auto* desc = FindOperator(OperatorID::DeviceCoord);
  ASSERT_NE(desc, nullptr);
  EXPECT_EQ(desc->returnType, SLType::Float2);
  ASSERT_EQ(desc->params.size(), 2u);
  EXPECT_EQ(desc->params[0], SLType::Float3x3);
  EXPECT_EQ(desc->params[1], SLType::Float2);
}

TEST(OperatorManifestTest, ExposesGaussianBlurSignature) {
  const auto* desc = FindOperator(OperatorID::GaussianBlur1D);
  ASSERT_NE(desc, nullptr);
  EXPECT_EQ(desc->returnType, SLType::Float4);
  ASSERT_EQ(desc->params.size(), 12u);
  EXPECT_EQ(desc->params[0], SLType::Texture2DSampler);
  EXPECT_EQ(desc->params[11], SLType::Float4);
}

TEST(OperatorManifestTest, RejectsUnknownOperators) {
  EXPECT_EQ(FindOperator(static_cast<OperatorID>(999)), nullptr);
}

}  // namespace tgfx

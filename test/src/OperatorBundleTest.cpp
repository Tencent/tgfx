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

#include "gpu/ir/OperatorBundle.h"
#include "gtest/gtest.h"

namespace tgfx {

TEST(OperatorBundleTest, FindsBackendArtifactsByOperatorAndBackend) {
  OperatorArtifact glsl = {};
  glsl.id = OperatorID::Luma;
  glsl.backend = OperatorBackend::GLSL;
  glsl.symbol = "pmApplyLuma";
  glsl.source = "vec4 pmApplyLuma(vec4 color, float kr, float kg, float kb);";

  OperatorArtifact spirv = {};
  spirv.id = OperatorID::Luma;
  spirv.backend = OperatorBackend::SPIRV;
  spirv.symbol = "pmApplyLuma";
  spirv.spirv = {0x07230203u, 0x00010000u};

  OperatorBundle bundle({glsl, spirv});
  const auto* glslArtifact = bundle.Find(OperatorID::Luma, OperatorBackend::GLSL);
  ASSERT_NE(glslArtifact, nullptr);
  EXPECT_EQ(glslArtifact->symbol, "pmApplyLuma");
  EXPECT_FALSE(glslArtifact->source.empty());
  EXPECT_TRUE(glslArtifact->spirv.empty());

  const auto* spirvArtifact = bundle.Find(OperatorID::Luma, OperatorBackend::SPIRV);
  ASSERT_NE(spirvArtifact, nullptr);
  EXPECT_EQ(spirvArtifact->symbol, "pmApplyLuma");
  EXPECT_EQ(spirvArtifact->spirv.size(), 2u);
  EXPECT_TRUE(spirvArtifact->source.empty());

  EXPECT_EQ(bundle.Find(OperatorID::Luma, OperatorBackend::MSL), nullptr);
  EXPECT_EQ(bundle.Find(OperatorID::ConstColor, OperatorBackend::GLSL), nullptr);
}

TEST(OperatorBundleTest, KeepsLogicalArrayParameterInManifest) {
  const auto* desc = FindOperator(OperatorID::GaussianBlur1D);
  ASSERT_NE(desc, nullptr);
  ASSERT_EQ(desc->params.size(), 12u);
  EXPECT_TRUE(desc->params[11].isArray);
  EXPECT_EQ(desc->params[11].arrayCount, 17u);
}

}  // namespace tgfx

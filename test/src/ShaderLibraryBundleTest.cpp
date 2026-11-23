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
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include <set>
#include <string>
#include "gpu/shaders/ShaderLibraryBundle.h"
#include "gtest/gtest.h"

namespace tgfx {

TEST(ShaderLibraryBundleTest, EmbeddedBundleCarriesGLSLFunctions) {
  const auto& bundle = ShaderLibraryBundle::Get();
  ASSERT_TRUE(bundle.valid());
  EXPECT_TRUE(bundle.hasLanguage(ShaderLibraryLanguage::GLSL));
  EXPECT_FALSE(bundle.hasLanguage(ShaderLibraryLanguage::MSL));
  EXPECT_GT(bundle.functionCount(ShaderLibraryLanguage::GLSL), 0u);
}

TEST(ShaderLibraryBundleTest, AppendFunctionExpandsDependencies) {
  std::set<std::string> emitted = {};
  std::string text = {};
  ASSERT_TRUE(ShaderLibraryBundle::Get().appendFunction(ShaderLibraryLanguage::GLSL, "pmApplyLuma",
                                                        &emitted, &text));
  EXPECT_FALSE(text.empty());
  EXPECT_NE(text.find("pmApplyLuma"), std::string::npos);
}

TEST(ShaderLibraryBundleTest, UnknownFunctionIsReported) {
  std::set<std::string> emitted = {};
  std::string text = {};
  EXPECT_FALSE(ShaderLibraryBundle::Get().appendFunction(
      ShaderLibraryLanguage::GLSL, "thisFunctionDoesNotExist", &emitted, &text));
}

}  // namespace tgfx

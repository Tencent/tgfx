/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. See the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include <filesystem>
#include <string>
#include "ScratchDir.h"
#include "core/ShaderSources.h"
#include "gtest/gtest.h"

namespace tgfx {
namespace {

const std::string RealShaderDir = std::string(TGFX_SOURCE_DIR) + "/src/gpu/shaders/glsl";

class ShaderSourcesTest : public ::testing::Test {
 protected:
  ShaderSourcesTest() : scratch("sources") {
    std::filesystem::copy(RealShaderDir, scratch.dir(), std::filesystem::copy_options::recursive);
  }

  ScratchDir scratch;
};

// Every family migrated to the declaration layer: their text is assembled from the module
// declarations and the stage includes are expanded by the build, so there are no handwritten
// family entries left to load. The loader and its include-resolution cases retire with the legacy
// registry; this test pins the end state until that deletion lands.
TEST_F(ShaderSourcesTest, TheDeclaredFamiliesLeaveNoHandwrittenEntries) {
  auto set = LoadShaderSources(scratch.dir());
  EXPECT_TRUE(set.ok) << (set.errors.empty() ? "" : set.errors[0]);
  EXPECT_TRUE(set.shaders.empty());
  EXPECT_EQ(set.find("SolidColor"), nullptr);
  EXPECT_EQ(set.find("Chain"), nullptr);
}

}  // namespace
}  // namespace tgfx

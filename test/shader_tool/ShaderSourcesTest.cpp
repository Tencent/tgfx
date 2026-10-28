//////////////////////////////////////////////////////////////////////////////////////////////////
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
//////////////////////////////////////////////////////////////////////////////////////////////////

// LoadShaderSources reads the shaders of the registered families, so these tests work on a copy of
// the real shader directory and break one thing in it per test. The copy keeps the real file names
// the families declare, and each test can name exactly what went wrong.

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include "ScratchDir.h"
#include "core/ShaderSources.h"
#include "gtest/gtest.h"

namespace tgfx {
namespace {

const std::string RealShaderDir = std::string(TGFX_SOURCE_DIR) + "/src/gpu/shaders/glsl";

// The family whose files the tests damage. Any registered family works; this one has a vertex and
// a fragment file of its own and includes shared headers.
constexpr char FamilyName[] = "SolidColorFillShader";

class ShaderSourcesTest : public ::testing::Test {
 protected:
  ShaderSourcesTest() : scratch("sources") {
    std::filesystem::copy(RealShaderDir, scratch.dir(), std::filesystem::copy_options::recursive);
  }

  std::string Read(const std::string& relativePath) const {
    std::ifstream file(scratch.dir() + "/" + relativePath);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
  }

  bool HasError(const ShaderSourceSet& set, const std::string& fragment) const {
    for (const auto& error : set.errors) {
      if (error.find(fragment) != std::string::npos) {
        return true;
      }
    }
    return false;
  }

  ScratchDir scratch;
};

}  // namespace

TEST_F(ShaderSourcesTest, TheRealSourcesLoadAndAreOrderedByName) {
  auto set = LoadShaderSources(scratch.dir());
  EXPECT_TRUE(set.ok) << (set.errors.empty() ? "" : set.errors[0]);
  ASSERT_FALSE(set.shaders.empty());
  for (size_t i = 1; i < set.shaders.size(); ++i) {
    EXPECT_LT(set.shaders[i - 1].info.name, set.shaders[i].info.name);
  }
  for (const auto& shader : set.shaders) {
    EXPECT_FALSE(shader.vertex.empty()) << shader.info.name;
    EXPECT_FALSE(shader.fragment.empty()) << shader.info.name;
    // Every include is expanded before the text reaches the compiler.
    EXPECT_EQ(shader.vertex.find("#include"), std::string::npos) << shader.info.name;
    EXPECT_EQ(shader.fragment.find("#include"), std::string::npos) << shader.info.name;
  }
}

TEST_F(ShaderSourcesTest, FindReturnsTheNamedShaderOrNull) {
  auto set = LoadShaderSources(scratch.dir());
  ASSERT_TRUE(set.ok);
  auto found = set.find(FamilyName);
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found->info.name, FamilyName);
  EXPECT_EQ(set.find("NoSuchShader"), nullptr);
}

TEST_F(ShaderSourcesTest, AMissingShaderFileIsAnError) {
  auto set = LoadShaderSources(scratch.dir());
  ASSERT_TRUE(set.ok);
  auto info = set.find(FamilyName)->info;
  std::filesystem::remove(scratch.dir() + "/" + info.fragmentFile);

  auto broken = LoadShaderSources(scratch.dir());
  EXPECT_FALSE(broken.ok);
  EXPECT_TRUE(HasError(broken, std::string(FamilyName) + " fragment: cannot read"));
}

TEST_F(ShaderSourcesTest, AnEmptyShaderFileIsAnError) {
  auto set = LoadShaderSources(scratch.dir());
  ASSERT_TRUE(set.ok);
  auto info = set.find(FamilyName)->info;
  scratch.write(info.vertexFile, "");

  auto broken = LoadShaderSources(scratch.dir());
  EXPECT_FALSE(broken.ok);
  EXPECT_TRUE(HasError(broken, std::string(FamilyName) + " vertex: ") &&
              HasError(broken, "is empty"));
}

TEST_F(ShaderSourcesTest, AnIncludeThatCannotBeReadIsAnError) {
  auto set = LoadShaderSources(scratch.dir());
  ASSERT_TRUE(set.ok);
  auto info = set.find(FamilyName)->info;
  scratch.write(info.fragmentFile, Read(info.fragmentFile) + "\n#include \"no_such_header.inc\"\n");

  auto broken = LoadShaderSources(scratch.dir());
  EXPECT_FALSE(broken.ok);
  EXPECT_TRUE(HasError(broken, "cannot read #include \"no_such_header.inc\""));
}

TEST_F(ShaderSourcesTest, AnIncludeCycleIsAnError) {
  auto set = LoadShaderSources(scratch.dir());
  ASSERT_TRUE(set.ok);
  auto info = set.find(FamilyName)->info;
  // Includes resolve against the shader root, so the fixtures state full root-relative paths
  // even though they sit next to each other in kernels/.
  scratch.write("kernels/cycle_a.inc", "#include \"kernels/cycle_b.inc\"\n");
  scratch.write("kernels/cycle_b.inc", "#include \"kernels/cycle_a.inc\"\n");
  scratch.write(info.vertexFile, Read(info.vertexFile) + "\n#include \"kernels/cycle_a.inc\"\n");

  auto broken = LoadShaderSources(scratch.dir());
  EXPECT_FALSE(broken.ok);
  EXPECT_TRUE(HasError(broken, "include cycle"));
  // The report names the whole chain, so the author can see where the loop closes.
  EXPECT_TRUE(HasError(broken, "cycle_a.inc -> ") && HasError(broken, "cycle_b.inc"));
}

TEST_F(ShaderSourcesTest, ASelfIncludeIsACycle) {
  auto set = LoadShaderSources(scratch.dir());
  ASSERT_TRUE(set.ok);
  auto info = set.find(FamilyName)->info;
  scratch.write("kernels/self.inc", "#include \"kernels/self.inc\"\n");
  scratch.write(info.vertexFile, Read(info.vertexFile) + "\n#include \"kernels/self.inc\"\n");

  auto broken = LoadShaderSources(scratch.dir());
  EXPECT_FALSE(broken.ok);
  EXPECT_TRUE(HasError(broken, "include cycle"));
}

TEST_F(ShaderSourcesTest, IncludesNestedTooDeepAreAnError) {
  auto set = LoadShaderSources(scratch.dir());
  ASSERT_TRUE(set.ok);
  auto info = set.find(FamilyName)->info;
  // The limit is part of what the tool promises, so the test states it instead of reading it: a
  // limit that grew would not fail a test that builds its chain from the constant.
  ASSERT_EQ(MaxIncludeDepth, 32);
  // A chain of distinct files, deeper than the limit: not a cycle, just too deep.
  const int links = 40;
  for (int i = 0; i < links; ++i) {
    std::string next = i + 1 < links
                           ? "#include \"kernels/deep_" + std::to_string(i + 1) + ".inc\"\n"
                           : "// end\n";
    scratch.write("kernels/deep_" + std::to_string(i) + ".inc", next);
  }
  scratch.write(info.vertexFile, Read(info.vertexFile) + "\n#include \"kernels/deep_0.inc\"\n");

  auto broken = LoadShaderSources(scratch.dir());
  EXPECT_FALSE(broken.ok);
  EXPECT_TRUE(HasError(broken, "includes nested deeper than"));
}

TEST_F(ShaderSourcesTest, IncludesWithinTheLimitAreAllowed) {
  auto set = LoadShaderSources(scratch.dir());
  ASSERT_TRUE(set.ok);
  auto info = set.find(FamilyName)->info;
  // Well inside the limit: the nesting check must not reject legitimate chains.
  const int links = 20;
  for (int i = 0; i < links; ++i) {
    std::string next = i + 1 < links
                           ? "#include \"kernels/shallow_" + std::to_string(i + 1) + ".inc\"\n"
                           : "// end\n";
    scratch.write("kernels/shallow_" + std::to_string(i) + ".inc", next);
  }
  scratch.write(info.vertexFile, Read(info.vertexFile) + "\n#include \"kernels/shallow_0.inc\"\n");

  auto loaded = LoadShaderSources(scratch.dir());
  EXPECT_TRUE(loaded.ok) << (loaded.errors.empty() ? "" : loaded.errors[0]);
}

TEST_F(ShaderSourcesTest, ARepeatedIncludeThatIsNotACycleIsAllowed) {
  auto set = LoadShaderSources(scratch.dir());
  ASSERT_TRUE(set.ok);
  auto info = set.find(FamilyName)->info;
  // The slot headers are guardless and included again and again on purpose.
  scratch.write("kernels/repeat.inc", "// repeated\n");
  scratch.write(info.vertexFile,
                Read(info.vertexFile) +
                    "\n#include \"kernels/repeat.inc\"\n#include \"kernels/repeat.inc\"\n"
                    "#include \"kernels/repeat.inc\"\n");

  auto repeated = LoadShaderSources(scratch.dir());
  EXPECT_TRUE(repeated.ok) << (repeated.errors.empty() ? "" : repeated.errors[0]);
}

TEST_F(ShaderSourcesTest, IncludesResolveAgainstTheShaderRootNotTheIncludingFile) {
  auto set = LoadShaderSources(scratch.dir());
  ASSERT_TRUE(set.ok);
  auto info = set.find(FamilyName)->info;
  // The include rule after the reorganization: every path resolves from the shader root, so a
  // header in one directory pulls a module from another without a "../" escape.
  scratch.write("coverage/uses_source.inc", "#include \"source/noop.inc\"\n");
  scratch.write("source/noop.inc", "// nothing\n");
  scratch.write(info.fragmentFile,
                Read(info.fragmentFile) + "\n#include \"coverage/uses_source.inc\"\n");

  auto loaded = LoadShaderSources(scratch.dir());
  EXPECT_TRUE(loaded.ok) << (loaded.errors.empty() ? "" : loaded.errors[0]);
}

TEST_F(ShaderSourcesTest, EveryFailureIsReportedNotJustTheFirst) {
  auto set = LoadShaderSources(scratch.dir());
  ASSERT_TRUE(set.ok);
  auto info = set.find(FamilyName)->info;
  std::filesystem::remove(scratch.dir() + "/" + info.vertexFile);
  std::filesystem::remove(scratch.dir() + "/" + info.fragmentFile);

  auto broken = LoadShaderSources(scratch.dir());
  EXPECT_FALSE(broken.ok);
  EXPECT_TRUE(HasError(broken, std::string(FamilyName) + " vertex:"));
  EXPECT_TRUE(HasError(broken, std::string(FamilyName) + " fragment:"));
}

}  // namespace tgfx

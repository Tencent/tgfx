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
#include <fstream>
#include <sstream>
#include <string>
#include "ScratchDir.h"
#include "core/ShaderSources.h"
#include "gtest/gtest.h"
#include "output/BundleManifest.h"

namespace tgfx {
namespace {

const std::string RealShaderDir = std::string(TGFX_SOURCE_DIR) + "/src/gpu/shaders/glsl";

// The contract text is read from the real source tree: a scratch copy of the shader directory
// does not carry the contract headers beside it (ReadGeneratorContract derives their directory
// from the shader directory), and the digest takes the text as a parameter anyway.
std::string RealGeneratorContract() {
  std::string text;
  std::string error;
  if (!ReadGeneratorContract(RealShaderDir, &text, &error)) {
    ADD_FAILURE() << error;
    return "";
  }
  return text;
}

void CopyTheShaderTree(const ScratchDir& scratch) {
  std::filesystem::copy(RealShaderDir, scratch.dir(), std::filesystem::copy_options::recursive);
}

std::string ReadFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

SourceDigestResult DigestOf(const ScratchDir& scratch, const std::string& contract) {
  auto result = ComputeSourceDigest(scratch.dir(), AllShaderProfiles[0], contract);
  EXPECT_TRUE(result.ok) << result.error;
  return result;
}

}  // namespace

TEST(SourceDigestTest, TheGeneratorContractIsReadable) {
  std::string text;
  std::string error;
  ASSERT_TRUE(ReadGeneratorContract(RealShaderDir, &text, &error)) << error;
  // The contract is the op table and the field lists it expands into; both must be in the text
  // the digest covers, or a layout change would not invalidate a bundle.
  EXPECT_NE(text.find("TGFX_CHAIN_OPS"), std::string::npos);
  EXPECT_NE(text.find("TGFX_CHAIN_OP_FIELD_LISTS"), std::string::npos);
}

TEST(SourceDigestTest, TheGeneratorContractMovesTheDigest) {
  ScratchDir scratch("digest-contract");
  CopyTheShaderTree(scratch);
  const auto contract = RealGeneratorContract();
  const auto base = DigestOf(scratch, contract);
  const auto mutated = DigestOf(scratch, contract + "\n// a layout change\n");
  ASSERT_TRUE(base.ok && mutated.ok);
  EXPECT_NE(base.digest, mutated.digest);
}

TEST(SourceDigestTest, TheDigestOfOneTreeIsStableAcrossRuns) {
  ScratchDir scratch("digest-stable");
  CopyTheShaderTree(scratch);
  const auto base = DigestOf(scratch, RealGeneratorContract());
  const auto repeated = DigestOf(scratch, RealGeneratorContract());
  ASSERT_TRUE(base.ok && repeated.ok);
  EXPECT_EQ(base.digest, repeated.digest);
}

TEST(SourceDigestTest, AHookOrIncludeChangeMovesTheDigest) {
  ScratchDir scratch("digest-tree");
  CopyTheShaderTree(scratch);
  const auto contract = RealGeneratorContract();
  const auto base = DigestOf(scratch, contract);
  ASSERT_TRUE(base.ok);
  // The hole this pins: the loaded-source digest never saw a module hook or a shared include —
  // every family's stage text is assembled, so no registered entry carried them.
  const auto hook = "modules/YUVTexture.frag.glsl";
  scratch.write(hook, ReadFile(scratch.dir() + "/" + hook) + "\n// a hook change\n");
  const auto hookMoved = DigestOf(scratch, contract);
  ASSERT_TRUE(hookMoved.ok);
  EXPECT_NE(base.digest, hookMoved.digest);
  const auto include = "xp/xp_blend_colors.inc";
  scratch.write(include, ReadFile(scratch.dir() + "/" + include) + "\n// an include change\n");
  const auto includeMoved = DigestOf(scratch, contract);
  ASSERT_TRUE(includeMoved.ok);
  EXPECT_NE(hookMoved.digest, includeMoved.digest);
}

TEST(SourceDigestTest, AFileOutsideTheWhitelistCannotMoveTheDigest) {
  ScratchDir scratch("digest-junk");
  CopyTheShaderTree(scratch);
  const auto contract = RealGeneratorContract();
  const auto base = DigestOf(scratch, contract);
  ASSERT_TRUE(base.ok);
  // .DS_Store and a non-shader extension are invisible: a machine's junk must not be able to
  // invalidate a bundle (nor, on the other side, hide behind a matched digest).
  scratch.write(".DS_Store", "junk");
  scratch.write("notes.txt", "junk");
  const auto after = DigestOf(scratch, contract);
  ASSERT_TRUE(after.ok);
  EXPECT_EQ(base.digest, after.digest);
}

}  // namespace tgfx

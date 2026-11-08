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
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. See the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include <map>
#include <string>
#include "core/ShaderSources.h"
#include "emit/StageEmitter.h"
#include "gtest/gtest.h"
#include "output/BundleManifest.h"

namespace tgfx {
namespace {

const std::string RealShaderDir = std::string(TGFX_SOURCE_DIR) + "/src/gpu/shaders/glsl";

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
  // The declared families' entries are generated; the digest must cover the compiled text.
  std::string emitError;
  const auto overrides = EmitDeclaredFragmentEntries(&emitError);
  ASSERT_TRUE(emitError.empty()) << emitError;
  auto sources = LoadShaderSources(RealShaderDir, &overrides);
  ASSERT_TRUE(sources.ok) << "the real shader sources must load";
  std::string text;
  std::string error;
  ASSERT_TRUE(ReadGeneratorContract(RealShaderDir, &text, &error)) << error;
  auto profile = AllShaderProfiles[0];
  auto base = ComputeSourceDigest(sources, profile, text);
  ASSERT_TRUE(base.ok) << base.error;
  auto repeated = ComputeSourceDigest(sources, profile, text);
  ASSERT_TRUE(repeated.ok) << repeated.error;
  EXPECT_EQ(base.digest, repeated.digest);
  auto mutated = ComputeSourceDigest(sources, profile, text + "\n// a layout change\n");
  ASSERT_TRUE(mutated.ok) << mutated.error;
  EXPECT_NE(base.digest, mutated.digest);
}

}  // namespace tgfx

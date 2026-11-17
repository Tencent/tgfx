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
//  either express or implied. see the license for the specific language governing permissions and
//  limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

// The stamp manifest is the build's proof that the bundles on disk are the ones this tool wrote:
// every failure it catches (a foreign tool, a tampered or missing bundle) must name the file and
// end with the same one-command fix, because the alternative is a build that quietly embeds a
// stale artifact.

#include <string>
#include <vector>
#include "ScratchDir.h"
#include "cli/BuildCommand.h"
#include "gtest/gtest.h"

namespace tgfx {
namespace {

TEST(StampManifestTest, RoundTripVerifiesTheWrittenBundles) {
  ScratchDir dir("stamp_roundtrip");
  dir.write("shader_bundle.opengl.bin", "opengl-payload");
  dir.write("shader_bundle.metal.bin", "metal-payload");
  const std::vector<StampFileEntry> entries = {{"opengl", dir.dir() + "/shader_bundle.opengl.bin"},
                                               {"metal", dir.dir() + "/shader_bundle.metal.bin"}};
  std::string error;
  ASSERT_TRUE(WriteStampManifest(dir.dir() + "/bundle_stamp", "tool-identity-a", entries, &error))
      << error;
  EXPECT_TRUE(VerifyStampManifest(dir.dir() + "/bundle_stamp", "tool-identity-a", &error)) << error;
}

TEST(StampManifestTest, ATamperedBundleFailsNamingTheFileAndTheFix) {
  ScratchDir dir("stamp_tamper");
  dir.write("shader_bundle.opengl.bin", "payload");
  const std::vector<StampFileEntry> entries = {{"opengl", dir.dir() + "/shader_bundle.opengl.bin"}};
  std::string error;
  ASSERT_TRUE(WriteStampManifest(dir.dir() + "/bundle_stamp", "tool-identity-a", entries, &error))
      << error;
  dir.write("shader_bundle.opengl.bin", "payload-changed");
  EXPECT_FALSE(VerifyStampManifest(dir.dir() + "/bundle_stamp", "tool-identity-a", &error));
  EXPECT_NE(error.find("shader_bundle.opengl.bin"), std::string::npos);
  EXPECT_NE(error.find("fix: delete"), std::string::npos);
}

TEST(StampManifestTest, AForeignToolFailsWithTheFixHint) {
  ScratchDir dir("stamp_foreign_tool");
  dir.write("shader_bundle.opengl.bin", "payload");
  const std::vector<StampFileEntry> entries = {{"opengl", dir.dir() + "/shader_bundle.opengl.bin"}};
  std::string error;
  // The identity prints truncated, so the two tools must differ inside the first 12 characters.
  ASSERT_TRUE(
      WriteStampManifest(dir.dir() + "/bundle_stamp", "aaaaaaaaaaaa-tool-a", entries, &error))
      << error;
  EXPECT_FALSE(VerifyStampManifest(dir.dir() + "/bundle_stamp", "bbbbbbbbbbbb-tool-b", &error));
  EXPECT_NE(error.find("aaaaaaaaaaaa"), std::string::npos);
  EXPECT_NE(error.find("bbbbbbbbbbbb"), std::string::npos);
  EXPECT_NE(error.find("fix: delete"), std::string::npos);
}

TEST(StampManifestTest, AMissingBundleFailsNamingTheFile) {
  ScratchDir dir("stamp_missing");
  dir.write("shader_bundle.opengl.bin", "payload");
  const std::vector<StampFileEntry> entries = {{"opengl", dir.dir() + "/shader_bundle.opengl.bin"}};
  std::string error;
  ASSERT_TRUE(WriteStampManifest(dir.dir() + "/bundle_stamp", "tool-identity-a", entries, &error))
      << error;
  std::error_code code;
  std::filesystem::remove(std::filesystem::path(dir.dir()) / "shader_bundle.opengl.bin", code);
  EXPECT_FALSE(VerifyStampManifest(dir.dir() + "/bundle_stamp", "tool-identity-a", &error));
  EXPECT_NE(error.find("shader_bundle.opengl.bin"), std::string::npos);
  EXPECT_NE(error.find("fix: delete"), std::string::npos);
}

TEST(StampManifestTest, AStampWithoutBundlesFails) {
  ScratchDir dir("stamp_empty");
  std::string error;
  ASSERT_TRUE(WriteStampManifest(dir.dir() + "/bundle_stamp", "tool-identity-a", {}, &error))
      << error;
  EXPECT_FALSE(VerifyStampManifest(dir.dir() + "/bundle_stamp", "tool-identity-a", &error));
  EXPECT_NE(error.find("no bundles"), std::string::npos);
}

}  // namespace
}  // namespace tgfx

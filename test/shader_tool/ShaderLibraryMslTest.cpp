//////////////////////////////////////////////////////////////////////////////////////////////////
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
//  license is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
//////////////////////////////////////////////////////////////////////////////////////////////////

#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "ScratchDir.h"
#include "cli/BuildCommand.h"
#include "compile/StageCache.h"
#include "compile/StageCompiler.h"
#include "gpu/shaders/contract/ShaderLibraryBundleFormat.h"
#include "gtest/gtest.h"

namespace tgfx {
namespace {

const std::string RealShaderDir = std::string(TGFX_SOURCE_DIR) + "/src/gpu/shaders/glsl";

uint32_t ReadU32(const std::vector<uint8_t>& data, size_t offset) {
  return static_cast<uint32_t>(data[offset]) | (static_cast<uint32_t>(data[offset + 1]) << 8) |
         (static_cast<uint32_t>(data[offset + 2]) << 16) |
         (static_cast<uint32_t>(data[offset + 3]) << 24);
}

struct SectionView {
  uint32_t language = 0;
  uint32_t functionCount = 0;
  uint32_t entryOffset = 0;
  uint32_t poolOffset = 0;
};

std::vector<SectionView> SectionsOf(const std::vector<uint8_t>& data) {
  std::vector<SectionView> sections = {};
  const uint32_t sectionCount = ReadU32(data, 16);
  for (uint32_t index = 0; index < sectionCount; ++index) {
    const size_t at =
        SHADER_LIBRARY_BUNDLE_HEADER_SIZE + index * SHADER_LIBRARY_BUNDLE_SECTION_SIZE;
    SectionView section = {};
    section.language = ReadU32(data, at);
    section.functionCount = ReadU32(data, at + 4);
    section.entryOffset = ReadU32(data, at + 8);
    section.poolOffset = ReadU32(data, at + 12);
    sections.push_back(section);
  }
  return sections;
}

std::string SourceOf(const std::vector<uint8_t>& data, const SectionView& section,
                     const std::string& name) {
  for (uint32_t index = 0; index < section.functionCount; ++index) {
    const size_t entry = section.entryOffset + index * SHADER_LIBRARY_BUNDLE_FUNCTION_SIZE;
    const uint32_t nameOffset = ReadU32(data, entry);
    const uint32_t sourceOffset = ReadU32(data, entry + 4);
    const uint32_t sourceSize = ReadU32(data, entry + 8);
    const std::string entryName(
        reinterpret_cast<const char*>(data.data() + section.poolOffset + nameOffset));
    if (entryName != name) {
      continue;
    }
    return std::string(
        reinterpret_cast<const char*>(data.data() + section.poolOffset + sourceOffset), sourceSize);
  }
  return "";
}

TEST(ShaderLibraryMslTest, WritesAMslSectionBesideTheGlslSection) {
  ScratchDir scratch("library_msl");
  const std::string bundlePath = scratch.dir() + "/shader_library.bundle";
  StageCache cache;
  StageCompiler compiler(&cache);
  std::string error;
  ASSERT_TRUE(WriteShaderLibraryBundle(RealShaderDir, bundlePath, &compiler, false, false, &error))
      << error;

  std::ifstream file(bundlePath, std::ios::binary);
  ASSERT_TRUE(file.is_open());
  std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
  ASSERT_GE(data.size(), SHADER_LIBRARY_BUNDLE_HEADER_SIZE);

  const auto sections = SectionsOf(data);
  ASSERT_EQ(sections.size(), 2u);
  EXPECT_EQ(sections[0].language, static_cast<uint32_t>(ShaderLibraryLanguage::GLSL));
  EXPECT_EQ(sections[1].language, static_cast<uint32_t>(ShaderLibraryLanguage::MSL));
  // Every GLSL function has an MSL counterpart; the section also carries the support type entry
  // and the sampler-argument list, which the GLSL section has no equivalent of.
  EXPECT_GE(sections[1].functionCount, sections[0].functionCount);
  EXPECT_LE(sections[1].functionCount, sections[0].functionCount + 2);

  const std::string source = SourceOf(data, sections[1], "transferTF");
  ASSERT_FALSE(source.empty());
  EXPECT_NE(source.find("transferTF"), std::string::npos);
  EXPECT_NE(source.find("float3"), std::string::npos);
  EXPECT_EQ(source.find("main0"), std::string::npos);
  EXPECT_EQ(source.find("#include"), std::string::npos);
}

TEST(ShaderLibraryMslTest, TheSupportEntryCarriesNoFunctionText) {
  ScratchDir scratch("library_msl_support");
  const std::string bundlePath = scratch.dir() + "/shader_library.bundle";
  StageCache cache;
  StageCompiler compiler(&cache);
  std::string error;
  ASSERT_TRUE(WriteShaderLibraryBundle(RealShaderDir, bundlePath, &compiler, false, false, &error))
      << error;

  std::ifstream file(bundlePath, std::ios::binary);
  ASSERT_TRUE(file.is_open());
  std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
  const auto sections = SectionsOf(data);
  ASSERT_EQ(sections.size(), 2u);
  const std::string support = SourceOf(data, sections[1], "__msl_support__");
  EXPECT_EQ(support.find("main0"), std::string::npos);
  EXPECT_EQ(support.find("#include"), std::string::npos);
}

}  // namespace
}  // namespace tgfx

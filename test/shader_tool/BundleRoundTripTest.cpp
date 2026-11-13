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

// The bundle format has two implementations: the writer in shader_build_tool and the loader in the
// runtime. Both read the layout from gpu/ShaderBundleContract.h, but a constant can be shared and still be
// used differently. These tests write a bundle with the tool and load it with the runtime, then
// compare every field the writer recorded with what the loader returns.

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "gpu/aot/bundle/PrecompiledShaderCache.h"
#include "gpu/shaders/contract/ShaderBundleContract.h"
#include "gtest/gtest.h"
#include "output/BundleWriter.h"

namespace tgfx {
namespace {

std::string TempPath(const std::string& name) {
  return ::testing::TempDir() + "tgfx_shader_tool_" + name;
}

std::vector<uint8_t> ReadFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
}

std::vector<uint8_t> Blob(const std::string& text) {
  return std::vector<uint8_t>(text.begin(), text.end());
}

// A variant with both stages, coded so a mixed-up pool shows up as a content mismatch.
VariantData FullVariant(const std::string& shader, uint32_t vertIndex, uint32_t fragIndex,
                        const std::string& profileTag, const std::string& vertexCode,
                        const std::string& fragmentCode) {
  VariantData variant;
  static uint32_t nextId = 1;
  variant.variantId = nextId++;
  variant.shaderName = shader;
  variant.vertPermutationIndex = vertIndex;
  variant.fragPermutationIndex = fragIndex;
  variant.profileTag = profileTag;
  variant.vertexBlob = Blob(vertexCode);
  variant.fragmentBlob = Blob(fragmentCode);
  return variant;
}

struct LoadedBundle {
  PrecompiledShaderCache cache;
  bool loaded = false;

  LoadedBundle() : cache(nullptr, Backend::Unknown) {
  }
};

void ExpectStageRoundTrips(const std::vector<uint8_t>& code, const ShaderStageBlob* blob) {
  ASSERT_NE(blob, nullptr);
  EXPECT_EQ(blob->data, code);
}

class BundleRoundTripTest : public ::testing::TestWithParam<bool> {};

std::string CompressionName(const ::testing::TestParamInfo<bool>& info) {
  return info.param ? std::string("Zstd") : std::string("Plain");
}

uint32_t ReadU32(const std::vector<uint8_t>& bytes, uint32_t offset) {
  return static_cast<uint32_t>(bytes[offset]) | (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
         (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
         (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

}  // namespace

TEST_P(BundleRoundTripTest, EveryFieldSurvivesTheLoader) {
  const bool compress = GetParam();
  const std::string tag = ProfileTag(ShaderProfile::Metal);
  std::vector<VariantData> variants = {
      FullVariant("FamilyA", 3, 7, tag, "vertex code of A3", "fragment code of A7"),
      FullVariant("FamilyA", 4, 7, tag, "vertex code of A4", "fragment code of A7"),
      FullVariant("FamilyB", 0, 1, tag, "vertex code of B0", "fragment code of B1"),
  };
  // Pad the code so the data pool is large enough for compression to pay off.
  for (auto& variant : variants) {
    variant.vertexBlob.resize(variant.vertexBlob.size() + 2000, 'v');
    variant.fragmentBlob.resize(variant.fragmentBlob.size() + 2000, 'f');
  }
  auto path = TempPath(compress ? "roundtrip_zstd.bin" : "roundtrip_plain.bin");
  Diagnostics diagnostics;
  ASSERT_TRUE(WriteBundle(path, tag, variants, compress, &diagnostics));
  EXPECT_TRUE(diagnostics.records().empty());
  auto bytes = ReadFile(path);
  ASSERT_GT(bytes.size(), BundleFormat::HeaderSize);

  // The writer stamps the version, the compression and the identity the loader requires.
  EXPECT_EQ(bytes[BundleFormat::Offset::FormatVersion], BundleFormat::CurrentVersion);
  EXPECT_EQ(bytes[BundleFormat::Offset::CompressionType],
            compress ? BundleFormat::Compression::Zstd : BundleFormat::Compression::None);

  LoadedBundle bundle;
  ASSERT_TRUE(bundle.cache.loadBundle(bytes.data(), bytes.size()));
  EXPECT_TRUE(bundle.cache.isLoaded());
  EXPECT_EQ(bundle.cache.profileTag(), tag);
  EXPECT_EQ(bundle.cache.bundleToolchainABI(), ExpectedToolchainABI);

  for (const auto& variant : variants) {
    ExpectStageRoundTrips(variant.vertexBlob, bundle.cache.findVertex(variant.variantId));
    ExpectStageRoundTrips(variant.fragmentBlob, bundle.cache.findFragment(variant.variantId));
  }
  std::remove(path.c_str());
}

TEST_P(BundleRoundTripTest, EveryVariantCarriesBothStages) {
  const bool compress = GetParam();
  const std::string tag = ProfileTag(ShaderProfile::OpenGL);
  // Every variant carries its own entry in both pools: the lookup key is the variant id, so two
  // variants never share an entry even when their bytes coincide.
  std::vector<VariantData> variants = {
      FullVariant("Shared", 1, 5, tag, "vertex one", "the one fragment"),
      FullVariant("Shared", 2, 5, tag, "vertex two", "the one fragment"),
  };
  auto path = TempPath(compress ? "shared_zstd.bin" : "shared_plain.bin");
  Diagnostics diagnostics;
  ASSERT_TRUE(WriteBundle(path, tag, variants, compress, &diagnostics));
  auto bytes = ReadFile(path);
  LoadedBundle bundle;
  ASSERT_TRUE(bundle.cache.loadBundle(bytes.data(), bytes.size()));
  EXPECT_EQ(bundle.cache.vertexEntryCount(), 2u);
  EXPECT_EQ(bundle.cache.fragmentEntryCount(), 2u);
  std::remove(path.c_str());
}

INSTANTIATE_TEST_SUITE_P(Compression, BundleRoundTripTest, ::testing::Bool(), CompressionName);

TEST(BundleHeaderTest, TheWriterFillsExactlyTheDocumentedLayout) {
  const std::string tag = ProfileTag(ShaderProfile::WebGPU);
  std::vector<VariantData> variants = {FullVariant("Layout", 0, 0, tag, "vert", "frag")};
  auto path = TempPath("layout.bin");
  Diagnostics diagnostics;
  ASSERT_TRUE(WriteBundle(path, tag, variants, false, &diagnostics));
  auto bytes = ReadFile(path);
  ASSERT_GE(bytes.size(), BundleFormat::HeaderSize);

  EXPECT_EQ(ReadU32(bytes, BundleFormat::Offset::Magic), BundleFormat::Magic);
  EXPECT_EQ(ReadU32(bytes, BundleFormat::Offset::ToolchainABI), ExpectedToolchainABI);
  EXPECT_EQ(ReadU32(bytes, BundleFormat::Offset::VertPoolCount), 1u);
  EXPECT_EQ(ReadU32(bytes, BundleFormat::Offset::FragPoolCount), 1u);
  // The pools follow the header directly and the data pool follows the pools.
  EXPECT_EQ(ReadU32(bytes, BundleFormat::Offset::VertPoolOffset), BundleFormat::HeaderSize);
  EXPECT_EQ(ReadU32(bytes, BundleFormat::Offset::FragPoolOffset),
            BundleFormat::HeaderSize + BundleFormat::PoolEntrySize);
  EXPECT_EQ(ReadU32(bytes, BundleFormat::Offset::DataOffset),
            BundleFormat::HeaderSize + 2 * BundleFormat::PoolEntrySize);
  // The profile tag is zero padded to its field.
  std::string storedTag(
      reinterpret_cast<const char*>(bytes.data()) + BundleFormat::Offset::ProfileTag,
      BundleFormat::ProfileTagSize);
  EXPECT_EQ(storedTag.substr(0, tag.size()), tag);
  EXPECT_EQ(storedTag.find_first_not_of('\0', tag.size()), std::string::npos);
  std::remove(path.c_str());
}

TEST(BundleHeaderTest, ThePayloadFormatFollowsTheProfileTag) {
  const std::string tag = ProfileTag(ShaderProfile::Metal);
  for (uint16_t kind : {BundleFormat::Payload::Text, BundleFormat::Payload::Binary}) {
    SCOPED_TRACE(kind);
    auto variant = FullVariant("Payload", 0, 0, tag, "vert", "frag");
    variant.payloadKind = kind;
    std::vector<VariantData> variants = {variant};
    auto path =
        TempPath(kind == BundleFormat::Payload::Binary ? "payload_binary.bin" : "payload_text.bin");
    Diagnostics diagnostics;
    ASSERT_TRUE(WriteBundle(path, tag, variants, false, &diagnostics));
    auto bytes = ReadFile(path);
    ASSERT_GE(bytes.size(), BundleFormat::HeaderSize);
    const auto offset = BundleFormat::Offset::PayloadFormat;
    // The field sits directly behind the profile tag, followed by the reserved half-word that
    // keeps the header 4-byte aligned.
    EXPECT_EQ(offset, BundleFormat::Offset::ProfileTag + BundleFormat::ProfileTagSize);
    EXPECT_EQ(bytes[offset] | (bytes[offset + 1] << 8), kind);
    EXPECT_EQ(bytes[offset + 2] | (bytes[offset + 3] << 8), 0);
    std::remove(path.c_str());
  }
}

TEST(BundleHeaderTest, AVariantListThatDisagreesOnThePayloadKindFails) {
  const std::string tag = ProfileTag(ShaderProfile::Metal);
  auto text = FullVariant("Mixed", 0, 0, tag, "vert", "frag");
  auto binary = FullVariant("Mixed", 1, 0, tag, "vert", "frag");
  binary.payloadKind = BundleFormat::Payload::Binary;
  std::vector<VariantData> variants = {text, binary};
  auto path = TempPath("mixed_payload.bin");
  Diagnostics diagnostics;
  EXPECT_FALSE(WriteBundle(path, tag, variants, false, &diagnostics));
  EXPECT_FALSE(diagnostics.records().empty());
  std::remove(path.c_str());
}

TEST(BundleHeaderTest, AnEmptyVariantListWritesNothing) {
  auto path = TempPath("empty.bin");
  std::remove(path.c_str());
  Diagnostics diagnostics;
  EXPECT_TRUE(WriteBundle(path, "opengl", {}, false, &diagnostics));
  EXPECT_TRUE(diagnostics.records().empty());
  EXPECT_TRUE(ReadFile(path).empty());
}

}  // namespace tgfx

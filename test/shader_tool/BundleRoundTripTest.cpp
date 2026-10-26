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
// runtime. Both read the layout from gpu/BundleFormat.h, but a constant can be shared and still be
// used differently. These tests write a bundle with the tool and load it with the runtime, then
// compare every field the writer recorded with what the loader returns.

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "BundleVerifier.h"
#include "BundleWriter.h"
#include "gpu/BundleFormat.h"
#include "gpu/PrecompiledBundleIdentity.h"
#include "gpu/PrecompiledShaderCache.h"
#include "gpu/ShaderKeyHash.h"
#include "gpu/ShaderProfile.h"
#include "gtest/gtest.h"

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

UniformEntry MakeUniform(const std::string& name, UniformFormat format, uint16_t arraySize = 1) {
  return UniformEntry{name, static_cast<uint8_t>(format), arraySize};
}

VertexInputEntry MakeVertexInput(const std::string& name, UniformFormat format, uint8_t location) {
  return VertexInputEntry{name, static_cast<uint8_t>(format), location};
}

// A variant with every kind of reflection entry the v5 format carries.
VariantData FullVariant(const std::string& shader, uint32_t vertIndex, uint32_t fragIndex,
                        const std::string& profileTag, const std::string& vertexCode,
                        const std::string& fragmentCode) {
  VariantData variant;
  variant.shaderName = shader;
  variant.vertPermutationIndex = vertIndex;
  variant.fragPermutationIndex = fragIndex;
  variant.profileTag = profileTag;
  variant.vertexBlob = Blob(vertexCode);
  variant.fragmentBlob = Blob(fragmentCode);
  variant.vertexReflection.uniforms = {MakeUniform("uMatrix", UniformFormat::Float3x3),
                                       MakeUniform("uOffset", UniformFormat::Float2)};
  // A gap between locations is legal and a real shape-instanced kernel has one.
  variant.vertexReflection.vertexInputs = {MakeVertexInput("aPosition", UniformFormat::Float2, 0),
                                           MakeVertexInput("aCoverage", UniformFormat::Float, 2)};
  variant.fragmentReflection.uniforms = {MakeUniform("uColor", UniformFormat::Float4),
                                         MakeUniform("uStops", UniformFormat::Float4, 8),
                                         MakeUniform("uMode", UniformFormat::Int)};
  variant.fragmentReflection.samplers = {MakeUniform("TextureSampler_0", UniformFormat::Texture2DSampler),
                                         MakeUniform("TextureSampler_1",
                                                 UniformFormat::TextureExternalSampler)};
  return variant;
}

struct LoadedBundle {
  PrecompiledShaderCache cache;
  bool loaded = false;

  LoadedBundle() : cache(nullptr, Backend::Unknown) {
  }
};

void ExpectSameUniforms(const std::vector<UniformEntry>& expected,
                        const std::vector<Uniform>& actual) {
  ASSERT_EQ(expected.size(), actual.size());
  for (size_t i = 0; i < expected.size(); ++i) {
    EXPECT_EQ(expected[i].name, actual[i].name()) << i;
    EXPECT_EQ(expected[i].format, static_cast<uint8_t>(actual[i].format())) << expected[i].name;
    EXPECT_EQ(expected[i].arraySize, actual[i].arraySize()) << expected[i].name;
  }
}

void ExpectStageRoundTrips(const std::vector<uint8_t>& code, const StageReflectionData& reflection,
                           const ShaderStageBlob* blob, bool vertex) {
  ASSERT_NE(blob, nullptr);
  EXPECT_EQ(blob->data, code);
  ExpectSameUniforms(reflection.uniforms, blob->uniforms);
  ExpectSameUniforms(reflection.samplers, blob->samplers);
  ASSERT_EQ(reflection.vertexInputs.size(), blob->vertexInputs.size());
  for (size_t i = 0; i < reflection.vertexInputs.size(); ++i) {
    EXPECT_EQ(reflection.vertexInputs[i].name, blob->vertexInputs[i].name);
    EXPECT_EQ(reflection.vertexInputs[i].format,
              static_cast<uint8_t>(blob->vertexInputs[i].format));
    EXPECT_EQ(reflection.vertexInputs[i].location, blob->vertexInputs[i].location);
  }
  // Only the vertex stage declares vertex inputs.
  if (!vertex) {
    EXPECT_TRUE(blob->vertexInputs.empty());
  }
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
  ASSERT_TRUE(WriteBundle(path, tag, variants, compress));
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
    auto vertexKey = ComputeVertexKeyHash(variant.shaderName, variant.vertPermutationIndex, tag);
    auto fragmentKey =
        ComputeFragmentKeyHash(variant.shaderName, variant.fragPermutationIndex, tag);
    ExpectStageRoundTrips(variant.vertexBlob, variant.vertexReflection,
                          bundle.cache.findVertex(vertexKey.hi, vertexKey.lo), true);
    ExpectStageRoundTrips(variant.fragmentBlob, variant.fragmentReflection,
                          bundle.cache.findFragment(fragmentKey.hi, fragmentKey.lo), false);
  }
  std::remove(path.c_str());
}

TEST_P(BundleRoundTripTest, SharedStagesAreStoredOnce) {
  const bool compress = GetParam();
  const std::string tag = ProfileTag(ShaderProfile::OpenGL);
  // Two variants use the same fragment index of the same family: one pool entry, two lookups.
  std::vector<VariantData> variants = {
      FullVariant("Shared", 1, 5, tag, "vertex one", "the one fragment"),
      FullVariant("Shared", 2, 5, tag, "vertex two", "the one fragment"),
  };
  auto path = TempPath(compress ? "shared_zstd.bin" : "shared_plain.bin");
  ASSERT_TRUE(WriteBundle(path, tag, variants, compress));
  auto bytes = ReadFile(path);
  LoadedBundle bundle;
  ASSERT_TRUE(bundle.cache.loadBundle(bytes.data(), bytes.size()));
  EXPECT_EQ(bundle.cache.vertexEntryCount(), 2u);
  EXPECT_EQ(bundle.cache.fragmentEntryCount(), 1u);
  std::remove(path.c_str());
}

INSTANTIATE_TEST_SUITE_P(Compression, BundleRoundTripTest, ::testing::Bool(), CompressionName);

TEST(BundleHeaderTest, TheWriterFillsExactlyTheDocumentedLayout) {
  const std::string tag = ProfileTag(ShaderProfile::WebGPU);
  std::vector<VariantData> variants = {FullVariant("Layout", 0, 0, tag, "vert", "frag")};
  auto path = TempPath("layout.bin");
  ASSERT_TRUE(WriteBundle(path, tag, variants, false));
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
  std::string storedTag(reinterpret_cast<const char*>(bytes.data()) + BundleFormat::Offset::ProfileTag,
                        BundleFormat::ProfileTagSize);
  EXPECT_EQ(storedTag.substr(0, tag.size()), tag);
  EXPECT_EQ(storedTag.find_first_not_of('\0', tag.size()), std::string::npos);
  std::remove(path.c_str());
}

TEST(BundleHeaderTest, AnEmptyVariantListWritesNothing) {
  auto path = TempPath("empty.bin");
  std::remove(path.c_str());
  EXPECT_TRUE(WriteBundle(path, "opengl", {}, false));
  EXPECT_TRUE(ReadFile(path).empty());
}

}  // namespace tgfx

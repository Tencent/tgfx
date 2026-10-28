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

// ContractChecks holds the build-time rules that keep the GLSL kernels, the C++ contracts and the
// runtime loader in step. A rule that never fires is worse than no rule, so every rule is tested
// both ways: the real sources and a well formed variant pass it, and each way of breaking the
// contract makes it report an error, with the reason in its diagnostic.

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "FailureAssertions.h"
#include "ScratchDir.h"
#include "gpu/Uniform.h"
#include "gpu/shaders/AllShaderFamilies.h"
#include "gpu/shaders/KernelContract.h"
#include "gtest/gtest.h"
#include "verify/ContractChecks.h"

namespace tgfx {
namespace {

const std::string RealShaderDir = std::string(TGFX_SOURCE_DIR) + "/src/gpu/shaders/glsl";

// What a rule reported: the error count it returned, the diagnostics it recorded, and their
// messages joined into one text so a test can check why as well as how often.
struct Report {
  size_t errors = 0;
  std::vector<Diagnostic> diagnostics;
  std::string log;

  bool mentions(const std::string& fragment) const {
    return log.find(fragment) != std::string::npos;
  }
};

Report Finish(size_t errors, const Diagnostics& diagnostics) {
  // Every check funnels through here, so the pairing between the returned violation count and
  // the failing records is pinned once for all of them.
  EXPECT_EQ(errors, FailuresIn(diagnostics));
  Report report;
  report.errors = errors;
  report.diagnostics = diagnostics.records();
  for (const auto& record : report.diagnostics) {
    report.log += record.message;
    report.log += '\n';
  }
  return report;
}

Report CheckClip(const std::vector<VariantData>& variants) {
  Diagnostics diagnostics;
  auto errors = ValidateClipContractFields(variants, &diagnostics);
  return Finish(errors, diagnostics);
}

Report CheckReflection(const std::vector<VariantData>& variants) {
  Diagnostics diagnostics;
  auto errors = ValidateReflectionContracts(variants, &diagnostics);
  return Finish(errors, diagnostics);
}

Report CheckChainOps(const std::string& shaderDir) {
  Diagnostics diagnostics;
  auto errors = ValidateChainOpCodes(shaderDir, &diagnostics);
  return Finish(errors, diagnostics);
}

Report CheckPointwiseOps(const std::string& shaderDir) {
  Diagnostics diagnostics;
  auto errors = ValidatePointwiseOpCodes(shaderDir, &diagnostics);
  return Finish(errors, diagnostics);
}

Report CheckSlotCapacities(const std::string& shaderDir) {
  Diagnostics diagnostics;
  auto errors = ValidateSlotCapacities(shaderDir, &diagnostics);
  return Finish(errors, diagnostics);
}

std::vector<std::string> RegisteredShaderNames() {
  std::vector<std::string> names;
  for (const auto& factory : ShaderRegistry::All()) {
    names.push_back(factory()->info().name);
  }
  return names;
}

Report CheckReferences(const std::string& shaderDir) {
  auto names = RegisteredShaderNames();
  Diagnostics diagnostics;
  auto errors = ValidateShaderSourceReferences(shaderDir, names, &diagnostics);
  return Finish(errors, diagnostics);
}

UniformEntry Field(const std::string& name, UniformFormat format = UniformFormat::Float4,
                   uint16_t arraySize = 1) {
  return UniformEntry{name, static_cast<uint8_t>(format), arraySize};
}

UniformEntry TextureSampler(const std::string& name) {
  return UniformEntry{name, static_cast<uint8_t>(UniformFormat::Texture2DSampler), 1};
}

std::vector<UniformEntry> ClipFields() {
  return {Field(ClipContract::Rect),
          Field(ClipContract::HasClip),
          Field(ClipContract::LocalRect),
          Field(ClipContract::RadiiX),
          Field(ClipContract::RadiiY),
          Field(ClipContract::AntiAlias),
          Field(ClipContract::DeviceToLocal, UniformFormat::Float3x3)};
}

VariantData VariantWithFragment(const std::vector<UniformEntry>& uniforms,
                                const std::vector<UniformEntry>& samplers = {}) {
  VariantData variant;
  variant.shaderName = "TestShader";
  variant.fragmentReflection.uniforms = uniforms;
  variant.fragmentReflection.samplers = samplers;
  return variant;
}

// A copy of the real shader directory that a test can damage.
class DamagedShaderDir : public ::testing::Test {
 protected:
  DamagedShaderDir() : scratch("contract") {
    std::filesystem::copy(RealShaderDir, scratch.dir(), std::filesystem::copy_options::recursive);
  }

  std::string Read(const std::string& relativePath) const {
    std::ifstream file(scratch.dir() + "/" + relativePath);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
  }

  // Replaces the first occurrence of from with to in a file of the copy; false if from is absent.
  bool Edit(const std::string& relativePath, const std::string& from, const std::string& to) {
    auto text = Read(relativePath);
    auto position = text.find(from);
    if (position == std::string::npos) {
      return false;
    }
    text.replace(position, from.size(), to);
    scratch.write(relativePath, text);
    return true;
  }

  void Append(const std::string& relativePath, const std::string& text) {
    scratch.write(relativePath, Read(relativePath) + text);
  }

  ScratchDir scratch;
};

}  // namespace

// ---- ValidateClipContractFields -------------------------------------------------------------

TEST(ClipContractTest, ACompleteContractPasses) {
  auto report = CheckClip({VariantWithFragment(ClipFields())});
  EXPECT_EQ(report.errors, 0u) << report.log;
}

TEST(ClipContractTest, AVariantWithoutTheMarkerIsNotChecked) {
  // Only a fragment block that carries HasClip is held to the contract.
  auto report = CheckClip({VariantWithFragment({Field("Color")})});
  EXPECT_EQ(report.errors, 0u) << report.log;
}

TEST(ClipContractTest, EachMissingFieldIsReported) {
  const auto all = ClipFields();
  for (size_t dropped = 0; dropped < all.size(); ++dropped) {
    if (all[dropped].name == ClipContract::HasClip) {
      continue;  // Without the marker the variant is not checked at all.
    }
    auto fields = all;
    fields.erase(fields.begin() + static_cast<std::ptrdiff_t>(dropped));
    auto report = CheckClip({VariantWithFragment(fields)});
    EXPECT_EQ(report.errors, 1u) << all[dropped].name;
    EXPECT_TRUE(report.mentions("'" + all[dropped].name + "'")) << report.log;
  }
}

TEST(ClipContractTest, EveryOffendingVariantIsCounted) {
  auto fields = ClipFields();
  fields.pop_back();
  std::vector<VariantData> variants(3, VariantWithFragment(fields));
  EXPECT_EQ(CheckClip(variants).errors, 3u);
}

// ---- ValidateChainOpCodes / ValidatePointwiseOpCodes ----------------------------------------

TEST_F(DamagedShaderDir, TheRealOpCodesAgreeWithTheContract) {
  auto chain = CheckChainOps(scratch.dir());
  EXPECT_EQ(chain.errors, 0u) << chain.log;
  auto pointwise = CheckPointwiseOps(scratch.dir());
  EXPECT_EQ(pointwise.errors, 0u) << pointwise.log;
}

TEST_F(DamagedShaderDir, ADriftedChainOpValueIsReported) {
  ASSERT_TRUE(Edit("operator/pointwise_chain_ops.inc", "#define OP_LUMA 1", "#define OP_LUMA 9"));
  auto report = CheckChainOps(scratch.dir());
  EXPECT_GE(report.errors, 1u);
  EXPECT_TRUE(report.mentions("OP_LUMA is 9 but the C++ contract says 1")) << report.log;
}

TEST_F(DamagedShaderDir, ARenamedChainOpIsReportedAsMissingAndAsStray) {
  ASSERT_TRUE(Edit("operator/pointwise_chain_ops.inc", "#define OP_BLEND", "#define OP_BLENDED"));
  auto report = CheckChainOps(scratch.dir());
  EXPECT_EQ(report.errors, 2u) << report.log;
  EXPECT_TRUE(report.mentions("OP define missing in operator/pointwise_chain_ops.inc: OP_BLEND"))
      << report.log;
  EXPECT_TRUE(report.mentions("unknown OP define in operator/pointwise_chain_ops.inc: OP_BLENDED"))
      << report.log;
}

TEST_F(DamagedShaderDir, AStrayChainOpIsReported) {
  Append("operator/pointwise_chain_ops.inc", "\n#define OP_EXTRA 99\n");
  auto report = CheckChainOps(scratch.dir());
  EXPECT_EQ(report.errors, 1u) << report.log;
  EXPECT_TRUE(report.mentions("unknown OP define in operator/pointwise_chain_ops.inc: OP_EXTRA"))
      << report.log;
}

TEST_F(DamagedShaderDir, ANonDecimalOpValueIsReported) {
  ASSERT_TRUE(
      Edit("operator/pointwise_chain_ops.inc", "#define OP_LUMA 1", "#define OP_LUMA 0 + 1"));
  auto report = CheckChainOps(scratch.dir());
  EXPECT_GE(report.errors, 1u);
  EXPECT_TRUE(report.mentions("cannot parse OP_LUMA value '0 + 1'")) << report.log;
}

TEST_F(DamagedShaderDir, ACommentedDefineCannotMaskARealMismatch) {
  // A line comment that repeats a define must not overwrite the value of the real one.
  ASSERT_TRUE(Edit("operator/pointwise_chain_ops.inc", "#define OP_LUMA 1",
                   "#define OP_LUMA 9\n// #define OP_LUMA 1"));
  auto report = CheckChainOps(scratch.dir());
  EXPECT_GE(report.errors, 1u);
  EXPECT_TRUE(report.mentions("OP_LUMA is 9")) << report.log;
}

TEST_F(DamagedShaderDir, AMissingOpFileIsReported) {
  std::filesystem::remove(scratch.dir() + "/operator/pointwise_chain_ops.inc");
  auto report = CheckChainOps(scratch.dir());
  EXPECT_EQ(report.errors, 1u);
  EXPECT_TRUE(report.mentions("cannot open operator/pointwise_chain_ops.inc")) << report.log;
}

TEST_F(DamagedShaderDir, ADriftedPointwiseOpValueIsReported) {
  ASSERT_TRUE(Edit("pipeline/slot/pointwise_op.inc", "#define OP_BLEND 7", "#define OP_BLEND 5"));
  auto report = CheckPointwiseOps(scratch.dir());
  EXPECT_GE(report.errors, 1u);
  EXPECT_TRUE(report.mentions("OP_BLEND is 5 but the C++ contract says 7")) << report.log;
}

TEST_F(DamagedShaderDir, AChainOnlyOpInThePointwiseFileIsReported) {
  Append("pipeline/slot/pointwise_op.inc", "\n#define OP_TEXTURE 5\n");
  auto report = CheckPointwiseOps(scratch.dir());
  EXPECT_EQ(report.errors, 1u) << report.log;
  EXPECT_TRUE(report.mentions("unknown OP define in pipeline/slot/pointwise_op.inc: OP_TEXTURE"))
      << report.log;
}

// ---- ValidateSlotCapacities -----------------------------------------------------------------

TEST_F(DamagedShaderDir, TheRealSlotCapacitiesAgreeWithTheContract) {
  auto report = CheckSlotCapacities(scratch.dir());
  EXPECT_EQ(report.errors, 0u) << report.log;
}

TEST_F(DamagedShaderDir, ADriftedSlotSuffixIsReported) {
  ASSERT_TRUE(Edit("kernels/perlin_noise.frag",
                   "TGFX_SLOT_ARRAY_SUFFIX [" + std::to_string(MaxPointwiseSlots) + "]",
                   "TGFX_SLOT_ARRAY_SUFFIX [" + std::to_string(MaxPointwiseSlots + 1) + "]"));
  auto report = CheckSlotCapacities(scratch.dir());
  EXPECT_EQ(report.errors, 1u) << report.log;
  EXPECT_TRUE(report.mentions("perlin_noise.frag slot capacity")) << report.log;
  EXPECT_TRUE(report.mentions("does not match MaxPointwiseSlots")) << report.log;
}

TEST_F(DamagedShaderDir, AMissingSlotSuffixIsReported) {
  ASSERT_TRUE(
      Edit("kernels/yuv_texture_fill.frag", "TGFX_SLOT_ARRAY_SUFFIX", "TGFX_SLOT_ARRAY_RENAMED"));
  auto report = CheckSlotCapacities(scratch.dir());
  EXPECT_GE(report.errors, 1u);
  EXPECT_TRUE(report.mentions("yuv_texture_fill.frag declares no TGFX_SLOT_ARRAY_SUFFIX"))
      << report.log;
}

TEST_F(DamagedShaderDir, ADriftedChainCapacityIsReported) {
  // Bump every capacity literal of the chain kernel: the contract expects the real ones.
  auto text = Read("kernels/pointwise_chain.frag");
  const std::string key = "#define CHAIN_SLOT_CAPACITY ";
  size_t position = 0;
  size_t edited = 0;
  while ((position = text.find(key, position)) != std::string::npos) {
    auto valueStart = position + key.size();
    auto valueEnd = text.find_first_not_of("0123456789", valueStart);
    auto value = std::stoul(text.substr(valueStart, valueEnd - valueStart));
    text.replace(valueStart, valueEnd - valueStart, std::to_string(value + 1));
    position = valueStart;
    ++edited;
  }
  ASSERT_GT(edited, 0u);
  scratch.write("kernels/pointwise_chain.frag", text);
  auto report = CheckSlotCapacities(scratch.dir());
  EXPECT_GE(report.errors, 1u);
  EXPECT_TRUE(report.mentions("pointwise_chain.frag capacities do not match")) << report.log;
}

TEST_F(DamagedShaderDir, AMissingChainCapacityIsReported) {
  auto text = Read("kernels/pointwise_chain.frag");
  const std::string key = "CHAIN_SLOT_CAPACITY";
  size_t position = 0;
  while ((position = text.find(key, position)) != std::string::npos) {
    text.replace(position, key.size(), "CHAIN_SLOT_RENAMED");
  }
  scratch.write("kernels/pointwise_chain.frag", text);
  auto report = CheckSlotCapacities(scratch.dir());
  EXPECT_GE(report.errors, 1u);
  EXPECT_TRUE(report.mentions("declares no CHAIN_SLOT_CAPACITY")) << report.log;
}

// ---- ValidateShaderSourceReferences ---------------------------------------------------------

TEST_F(DamagedShaderDir, TheRealSourcesReferenceOnlyThingsThatExist) {
  auto report = CheckReferences(scratch.dir());
  EXPECT_EQ(report.errors, 0u) << report.log;
}

TEST_F(DamagedShaderDir, AReferenceToAMissingFileIsReported) {
  Append("kernels/solid_color_fill.frag", "\n// see removed_header.inc\n");
  auto report = CheckReferences(scratch.dir());
  EXPECT_EQ(report.errors, 1u) << report.log;
  EXPECT_TRUE(report.mentions("refers to removed_header.inc, which does not exist")) << report.log;
}

TEST_F(DamagedShaderDir, AReferenceToAnUnregisteredFamilyIsReported) {
  Append("kernels/solid_color_fill.frag", "\n// formerly GoneAwayShader\n");
  auto report = CheckReferences(scratch.dir());
  EXPECT_EQ(report.errors, 1u) << report.log;
  EXPECT_TRUE(report.mentions("refers to shader family GoneAwayShader")) << report.log;
}

TEST_F(DamagedShaderDir, PlaceholdersAndExternalNamesAreExempt) {
  Append("kernels/solid_color_fill.frag",
         "\n// <module>_uniforms.inc, pointwise_*.inc, SwiftShader\n");
  auto report = CheckReferences(scratch.dir());
  EXPECT_EQ(report.errors, 0u) << report.log;
}

TEST(SourceReferencesTest, AnUnreadableDirectoryIsReported) {
  auto report = CheckReferences("/nonexistent/shader/dir");
  EXPECT_EQ(report.errors, 1u);
  EXPECT_TRUE(report.mentions("cannot list shader sources")) << report.log;
}

// ---- ValidateReflectionContracts ------------------------------------------------------------

TEST(ReflectionContractTest, AWellFormedVariantPasses) {
  auto variant =
      VariantWithFragment({Field("Color"), Field("Stops", UniformFormat::Float4, 8)},
                          {TextureSampler("TextureSampler_0"), TextureSampler("TextureSampler_1"),
                           TextureSampler(SamplerContract::DstTexture)});
  auto report = CheckReflection({variant});
  EXPECT_EQ(report.errors, 0u) << report.log;
}

TEST(ReflectionContractTest, ADuplicateNameInOneStageIsReported) {
  auto report = CheckReflection({VariantWithFragment({Field("Color"), Field("Color")})});
  EXPECT_EQ(report.errors, 1u) << report.log;
  EXPECT_TRUE(report.mentions("declares duplicate name 'Color'")) << report.log;
}

TEST(ReflectionContractTest, ASamplerNamedLikeAUniformIsADuplicate) {
  auto report = CheckReflection(
      {VariantWithFragment({Field("TextureSampler_0")}, {TextureSampler("TextureSampler_0")})});
  EXPECT_GE(report.errors, 1u);
  EXPECT_TRUE(report.mentions("duplicate name")) << report.log;
}

TEST(ReflectionContractTest, AnArraySizeOfZeroIsReported) {
  auto report = CheckReflection({VariantWithFragment({Field("Stops", UniformFormat::Float4, 0)})});
  EXPECT_EQ(report.errors, 1u) << report.log;
  EXPECT_TRUE(report.mentions("has arraySize 0")) << report.log;
}

TEST(ReflectionContractTest, ASamplerArrayIsReported) {
  auto sampler = TextureSampler("TextureSampler_0");
  sampler.arraySize = 2;
  auto report = CheckReflection({VariantWithFragment({}, {sampler})});
  EXPECT_EQ(report.errors, 1u) << report.log;
  EXPECT_TRUE(report.mentions("samplers need exactly 1")) << report.log;
}

TEST(ReflectionContractTest, ANameWithAnEmbeddedNulIsReported) {
  auto report = CheckReflection({VariantWithFragment({Field(std::string("Co\0lor", 6))})});
  EXPECT_EQ(report.errors, 1u) << report.log;
  EXPECT_TRUE(report.mentions("embedded NUL")) << report.log;
}

TEST(ReflectionContractTest, ANameOverTheSerializationLimitIsReported) {
  auto report = CheckReflection({VariantWithFragment({Field(std::string(256, 'n'))})});
  EXPECT_EQ(report.errors, 1u) << report.log;
  EXPECT_TRUE(report.mentions("exceeds the 255-byte name serialization limit")) << report.log;
}

TEST(ReflectionContractTest, ANameAtTheSerializationLimitPasses) {
  auto report = CheckReflection({VariantWithFragment({Field(std::string(255, 'n'))})});
  EXPECT_EQ(report.errors, 0u) << report.log;
}

TEST(ReflectionContractTest, MoreThanTwoHundredFiftyFiveEntriesAreReported) {
  std::vector<UniformEntry> uniforms;
  for (int i = 0; i < 256; ++i) {
    uniforms.push_back(Field("u" + std::to_string(i)));
  }
  auto report = CheckReflection({VariantWithFragment(uniforms)});
  EXPECT_GE(report.errors, 1u);
  EXPECT_TRUE(report.mentions("255-entry serialization limit")) << report.log;
}

TEST(ReflectionContractTest, AGenericSamplerWithAGapIsReported) {
  auto report = CheckReflection({VariantWithFragment(
      {}, {TextureSampler("TextureSampler_0"), TextureSampler("TextureSampler_2")})});
  EXPECT_EQ(report.errors, 1u) << report.log;
  EXPECT_TRUE(report.mentions("indices must be dense from zero")) << report.log;
}

TEST(ReflectionContractTest, ASamplerOutsideBothNamingSchemesIsReported) {
  auto report = CheckReflection({VariantWithFragment({}, {TextureSampler("Texture")})});
  EXPECT_EQ(report.errors, 1u) << report.log;
  EXPECT_TRUE(report.mentions("neither the TextureSampler_<N> pattern nor a registered semantic"))
      << report.log;
}

TEST(ReflectionContractTest, ASamplerWithANonDigitSuffixIsReported) {
  auto report = CheckReflection({VariantWithFragment({}, {TextureSampler("TextureSampler_x")})});
  EXPECT_EQ(report.errors, 1u) << report.log;
}

TEST(ReflectionContractTest, SemanticSamplersDoNotCountTowardsTheGenericIndices) {
  // A semantic sampler between two generic ones leaves the generic indices dense.
  auto report = CheckReflection({VariantWithFragment(
      {}, {TextureSampler("TextureSampler_0"), TextureSampler(SamplerContract::DstTexture),
           TextureSampler("TextureSampler_1")})});
  EXPECT_EQ(report.errors, 0u) << report.log;
}

TEST(ReflectionContractTest, TheVertexStageIsCheckedToo) {
  VariantData variant;
  variant.shaderName = "TestShader";
  variant.vertexReflection.uniforms = {Field("Matrix"), Field("Matrix")};
  auto report = CheckReflection({variant});
  EXPECT_EQ(report.errors, 1u) << report.log;
  EXPECT_TRUE(report.mentions("duplicate name 'Matrix'")) << report.log;
}

}  // namespace tgfx

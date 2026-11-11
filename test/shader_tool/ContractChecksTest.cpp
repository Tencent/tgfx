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
#include "gpu/shaders/Families.h"
#include "gpu/shaders/PrecompiledShader.h"
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

std::vector<std::string> RegisteredShaderNames() {
  // Every family is a row of the declaration table; the reference check resolves those names.
  std::vector<std::string> names;
  for (size_t index = 0; index < FamilyCount(); ++index) {
    names.push_back(Families()[index].name);
  }
  return names;
}

Report CheckReferences(const std::string& shaderDir) {
  auto names = RegisteredShaderNames();
  Diagnostics diagnostics;
  auto errors = ValidateShaderSourceReferences(shaderDir, names, &diagnostics);
  return Finish(errors, diagnostics);
}


Report CheckFamilyParams(const std::vector<VariantData>& variants) {
  Diagnostics diagnostics;
  auto errors = ValidateFamilyParams(variants, &diagnostics);
  return Finish(errors, diagnostics);
}

Report CheckMacroNames(const std::string& shaderDir) {
  Diagnostics diagnostics;
  auto errors = ValidateMacroNames(shaderDir, &diagnostics);
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
  // The gap index must not be a declared module's texture name: those are legal by construction
  // (the Chain module declares TextureSampler_0..3), so the synthetic gap uses a free index.
  auto report = CheckReflection({VariantWithFragment(
      {}, {TextureSampler("TextureSampler_0"), TextureSampler("TextureSampler_9")})});
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

// ---- the declared-name reconciliation -------------------------------------------------------

TEST(FamilyParamsTest, AReflectedNameWithoutADeclarationIsReported) {
  auto report = CheckFamilyParams({VariantWithFragment({Field("NotARealField")})});
  EXPECT_TRUE(report.mentions("'NotARealField', which no module or catalog constant declares"))
      << report.log;
}

TEST(FamilyParamsTest, ADeclaredTypeMismatchIsReported) {
  // Subset is declared Float4; a Float2 reflection is a declaration that drifted.
  auto report = CheckFamilyParams({VariantWithFragment({Field("Subset", UniformFormat::Float2)})});
  EXPECT_TRUE(report.mentions("reflects format")) << report.log;
}

TEST(FamilyParamsTest, TheStructuralSuffixResolvesToTheDeclaredBase) {
  auto report = CheckFamilyParams({VariantWithFragment({Field("Subset_1")})});
  EXPECT_FALSE(report.mentions("'Subset_1'")) << report.log;
  EXPECT_FALSE(report.mentions("declared parameter 'Subset' is carried")) << report.log;
}

TEST(FamilyParamsTest, ADeclaredConstantNoVariantCarriesIsReported) {
  // Only Subset is reflected, so every other declared constant is unused; AlphaStart is one.
  auto report = CheckFamilyParams({VariantWithFragment({Field("Subset")})});
  EXPECT_TRUE(report.mentions("declared parameter 'AlphaStart' is carried by no variant"))
      << report.log;
}

TEST(FamilyParamsTest, ANameEndingInDigitsStaysItself) {
  // TiledSubset2 is itself declared; stripping its trailing digits would resolve it to
  // TiledSubset, which is declared too, so the wrong field would answer for it.
  auto report = CheckFamilyParams({VariantWithFragment({Field("TiledSubset2")})});
  EXPECT_FALSE(report.mentions("'TiledSubset2'")) << report.log;
}

// ---- the macro-name rule --------------------------------------------------------------------

TEST(MacroNamesTest, TheRealSourcesPass) {
  auto report = CheckMacroNames(RealShaderDir);
  EXPECT_EQ(report.errors, 0u) << report.log;
}

TEST(MacroNamesTest, AnUnknownMacroIsReported) {
  const ScratchDir dir("macro_names_unknown");
  dir.write("kernels/bad.frag", "#ifdef NOT_A_REAL_MACRO\n#endif\n");
  auto report = CheckMacroNames(dir.dir());
  EXPECT_TRUE(report.mentions("conditions on 'NOT_A_REAL_MACRO'")) << report.log;
}

TEST(MacroNamesTest, AFamilyDimensionAndASourceDefinePass) {
  const ScratchDir dir("macro_names_known");
  dir.write("kernels/good.frag",
            "#define LOCAL_FLAG 1\n#ifdef LOCAL_FLAG\n#endif\n#if defined(PROFILE)\n#endif\n");
  auto report = CheckMacroNames(dir.dir());
  EXPECT_EQ(report.errors, 0u) << report.log;
}

}  // namespace tgfx

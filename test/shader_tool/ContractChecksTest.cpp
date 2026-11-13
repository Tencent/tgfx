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
#include "gpu/shaders/KernelContract.h"
#include "gpu/shaders/PrecompiledShader.h"
#include "gtest/gtest.h"
#include "verify/ContractChecks.h"

namespace tgfx {
namespace {

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

Report CheckReflection(const std::vector<VariantData>& variants) {
  Diagnostics diagnostics;
  auto errors = ValidateReflectionContracts(variants, &diagnostics);
  return Finish(errors, diagnostics);
}
UniformEntry Field(const std::string& name, UniformFormat format = UniformFormat::Float4,
                   uint16_t arraySize = 1) {
  return UniformEntry{name, static_cast<uint8_t>(format), arraySize};
}

UniformEntry TextureSampler(const std::string& name) {
  return UniformEntry{name, static_cast<uint8_t>(UniformFormat::Texture2DSampler), 1};
}

VariantData VariantWithFragment(const std::vector<UniformEntry>& uniforms,
                                const std::vector<UniformEntry>& samplers = {}) {
  VariantData variant;
  variant.shaderName = "TestShader";
  variant.fragmentReflection.uniforms = uniforms;
  variant.fragmentReflection.samplers = samplers;
  return variant;
};

}  // namespace

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
}  // namespace tgfx

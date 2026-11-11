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
//  Unless required by applicable law or agreed to in writing, software distributed under
//  the license is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
//  KIND, either express or implied. see the license for the specific language governing
//  permissions and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

// The binding-layout rule is what keeps the layout drift of the pre-contract era from coming
// back: a stored variant that declares a resource outside the three groups/sets the runtime
// pipeline binds would fail every draw at load. These tests feed the rule synthetic variants —
// a well-formed one, and one for each way of drifting — so the rule itself is pinned, not just
// exercised on the (currently clean) real build.

#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>
#include "FailureAssertions.h"
#include "core/DiagnosticCodes.h"
#include "gpu/shaders/contract/ShaderBundleContract.h"
#include "gtest/gtest.h"
#include "verify/BindingLayoutRule.h"

namespace tgfx {
namespace {

std::vector<uint8_t> BytesOf(const std::string& text) {
  return std::vector<uint8_t>(text.begin(), text.end());
}

// A minimal SPIR-V module whose only content is one decorated id per (set, binding) pair.
std::vector<uint8_t> SpirvWithDecorations(const std::vector<std::pair<int, int>>& setBindings) {
  std::vector<uint32_t> words = {0x07230203u, 0x00010000u, 0u, 64u, 0u};
  uint32_t id = 1;
  for (const auto& setBinding : setBindings) {
    words.push_back((4u << 16) | 71u);  // OpDecorate, 4 words
    words.push_back(id);
    words.push_back(34u);  // Decoration: DescriptorSet
    words.push_back(static_cast<uint32_t>(setBinding.first));
    words.push_back((4u << 16) | 71u);
    words.push_back(id);
    words.push_back(33u);  // Decoration: Binding
    words.push_back(static_cast<uint32_t>(setBinding.second));
    id++;
  }
  std::vector<uint8_t> bytes(words.size() * 4);
  std::memcpy(bytes.data(), words.data(), bytes.size());
  return bytes;
}

// A WebGPU variant whose stages spell the contract layout in tint's qualifier order. The vertex
// stage carries a real entry point and the reflection records its parameter slot: a stage the
// rule cannot read fails closed (VerifyNotApplicable), so the fixture must be one it can read.
VariantData WebgpuVariant(const std::string& fragText, size_t samplerCount) {
  VariantData variant;
  variant.shaderName = "TestShader";
  variant.profileTag = "webgpu";
  variant.vertexBlob = BytesOf(
      "@binding(0) @group(0) var<uniform> x : S;\n"
      "\n"
      "fn main(@location(0) pos : vec4<f32>) -> @builtin(position) vec4<f32> {\n"
      "  return pos;\n"
      "}\n");
  variant.vertexReflection.vertexInputs.push_back({"aPosition", 0, 0});
  variant.fragmentBlob = BytesOf(fragText);
  variant.fragmentReflection.samplers.resize(samplerCount);
  return variant;
}

// One fragment text with the fragment UBO and n texture pairs in group 2, binding-first.
std::string WebgpuFragmentText(int pairCount) {
  std::string text = "@binding(1) @group(1) var<uniform> u : S;\n";
  for (int i = 0; i < pairCount; ++i) {
    auto pair = SamplerBinding(ShaderProfile::WebGPU, i);
    text += "@binding(" + std::to_string(pair.first) + ") @group(2) var t" + std::to_string(i) +
            " : texture_2d<f32>;\n";
    text += "@binding(" + std::to_string(pair.second) + ") @group(2) var s" + std::to_string(i) +
            " : sampler;\n";
  }
  return text;
}

ArtifactRuleContext ContextOf(const std::vector<VariantData>& variants,
                              const std::vector<InlineExpansionRecord>& expansions) {
  ArtifactRuleContext context;
  context.variants = &variants;
  context.inlineExpansions = &expansions;
  return context;
}

BindingLayoutRule rule;

std::vector<InlineExpansionRecord> noExpansions;

// Runs the rule over the variants and returns the violation count; the per-test diagnostics
// stay local, so a test that cares what was reported builds its own Diagnostics.
size_t CheckVariants(const std::vector<VariantData>& variants) {
  Diagnostics diagnostics;
  auto errors = rule.check(ContextOf(variants, noExpansions), &diagnostics);
  EXPECT_EQ(errors, FailuresIn(diagnostics));
  return errors;
}

}  // namespace

TEST(BindingLayoutRuleTest, AContractWebgpuVariantPasses) {
  std::vector<VariantData> variants = {WebgpuVariant(WebgpuFragmentText(2), 2)};
  EXPECT_EQ(CheckVariants(variants), 0u);
}

TEST(BindingLayoutRuleTest, BothQualifierOrdersAreUnderstood) {
  // @group first in the text must read the same as @binding first: tint writes one order, but
  // the rule must not depend on it.
  VariantData groupFirst = WebgpuVariant(WebgpuFragmentText(1), 1);
  groupFirst.fragmentBlob = BytesOf(
      "@group(1) @binding(1) var<uniform> u : S;\n@group(2) @binding(0) var t0 : "
      "texture_2d<f32>;\n@group(2) @binding(1) var s0 : sampler;\n");
  std::vector<VariantData> variants = {groupFirst};
  EXPECT_EQ(CheckVariants(variants), 0u);
}

TEST(BindingLayoutRuleTest, AWgslWithFewerInputsThanTheReflectionPasses) {
  // The shaderc performance pass legally drops vertex inputs the body never uses, so the WGSL
  // entry may declare fewer parameters than the reflection — here slot 4 is gone — as long as
  // every slot it does read is one the reflection (and so the runtime) covers.
  VariantData variant = WebgpuVariant(WebgpuFragmentText(1), 1);
  // The stage still declares its vertex UBO (every tint emission does); only the parameter list
  // differs from the default fixture.
  variant.vertexBlob = BytesOf(
      "@binding(0) @group(0) var<uniform> x : S;\n"
      "@vertex\nfn main(@location(0) aPosition : vec2f, @location(1) inCoverage : f32, "
      "@location(2) inEllipseOffset : vec2f, @location(3) inEllipseRadii : vec2f, "
      "@location(5) inColor : vec4f) -> main_out {\n  return main_out();\n}\n");
  for (uint32_t location : {0u, 1u, 2u, 3u, 4u, 5u}) {
    VertexInputEntry input = {};
    input.location = static_cast<uint8_t>(location);
    variant.vertexReflection.vertexInputs.push_back(input);
  }
  std::vector<VariantData> variants = {variant};
  EXPECT_EQ(CheckVariants(variants), 0u);
}

TEST(BindingLayoutRuleTest, AWgslVertexSlotOutsideTheReflectionFails) {
  // A parameter on a slot the reflection does not cover reads whatever the runtime layout
  // happens to leave there — the misalignment this rule exists to catch.
  VariantData variant = WebgpuVariant(WebgpuFragmentText(1), 1);
  variant.vertexBlob = BytesOf(
      "@vertex\nfn main(@location(0) aPosition : vec2f, @location(7) stray : f32) -> main_out {\n"
      "  return main_out();\n}\n");
  VertexInputEntry input = {};
  input.name = "aPosition";
  input.location = 0;
  variant.vertexReflection.vertexInputs.push_back(input);
  std::vector<VariantData> variants = {variant};
  EXPECT_GE(CheckVariants(variants), 1u);
}

TEST(BindingLayoutRuleTest, AFragmentResourceInAGroupThePipelineNeverBindsFails) {
  auto text = WebgpuFragmentText(1) + "@binding(0) @group(3) var stray : f32;\n";
  std::vector<VariantData> variants = {WebgpuVariant(text, 1)};
  EXPECT_GE(CheckVariants(variants), 1u);
}

TEST(BindingLayoutRuleTest, AFragmentUboOnTheWrongBindingFails) {
  VariantData variant = WebgpuVariant(WebgpuFragmentText(0), 0);
  variant.fragmentBlob = BytesOf("@binding(2) @group(1) var<uniform> u : S;\n");
  std::vector<VariantData> variants = {variant};
  EXPECT_GE(CheckVariants(variants), 1u);
}

TEST(BindingLayoutRuleTest, AMissingTexturePairFails) {
  // Two samplers are reflected, but the text only carries one pair.
  std::vector<VariantData> variants = {WebgpuVariant(WebgpuFragmentText(1), 2)};
  EXPECT_GE(CheckVariants(variants), 1u);
}

TEST(BindingLayoutRuleTest, AVertexResourceOutsideTheVertexSetFails) {
  VariantData variant = WebgpuVariant(WebgpuFragmentText(1), 1);
  variant.vertexBlob = BytesOf("@binding(0) @group(2) var<uniform> x : S;\n");
  std::vector<VariantData> variants = {variant};
  EXPECT_GE(CheckVariants(variants), 1u);
}

TEST(BindingLayoutRuleTest, AContractVulkanVariantPasses) {
  VariantData variant;
  variant.shaderName = "TestShader";
  variant.profileTag = "vulkan";
  variant.vertexBlob =
      SpirvWithDecorations({{VERTEX_UBO_DESCRIPTOR_SET, VERTEX_UBO_BINDING_POINT}});
  variant.fragmentBlob =
      SpirvWithDecorations({{FRAGMENT_UBO_DESCRIPTOR_SET, FRAGMENT_UBO_BINDING_POINT},
                            {TEXTURE_DESCRIPTOR_SET, 0},
                            {TEXTURE_DESCRIPTOR_SET, 1}});
  std::vector<VariantData> variants = {variant};
  EXPECT_EQ(CheckVariants(variants), 0u);
}

TEST(BindingLayoutRuleTest, AVulkanFragmentResourceInSetZeroFails) {
  // The fragment stage of the pre-contract layout: the fragment UBO declared in the vertex
  // set, where the pipeline binds only the vertex UBO's descriptor set.
  VariantData variant;
  variant.shaderName = "TestShader";
  variant.profileTag = "vulkan";
  variant.vertexBlob =
      SpirvWithDecorations({{VERTEX_UBO_DESCRIPTOR_SET, VERTEX_UBO_BINDING_POINT}});
  variant.fragmentBlob = SpirvWithDecorations(
      {{VERTEX_UBO_DESCRIPTOR_SET, FRAGMENT_UBO_BINDING_POINT}, {TEXTURE_DESCRIPTOR_SET, 0}});
  std::vector<VariantData> variants = {variant};
  EXPECT_GE(CheckVariants(variants), 1u);
}

TEST(BindingLayoutRuleTest, AVulkanUboOnTheWrongBindingFails) {
  VariantData variant;
  variant.shaderName = "TestShader";
  variant.profileTag = "vulkan";
  variant.vertexBlob = SpirvWithDecorations({{VERTEX_UBO_DESCRIPTOR_SET, 3}});
  std::vector<VariantData> variants = {variant};
  EXPECT_GE(CheckVariants(variants), 1u);
}

// The D3D12 storage is HLSL, whose registers the root signature is built from. `vertexText` and
// `fragmentText` are the register-carrying parts of the emitted sources; the surrounding HLSL does
// not affect the rule.
VariantData D3D12Variant(const std::string& vertexText, const std::string& fragmentText) {
  VariantData variant;
  variant.shaderName = "TestShader";
  variant.profileTag = "d3d12";
  variant.vertexBlob = BytesOf(vertexText);
  variant.fragmentBlob = BytesOf(fragmentText);
  return variant;
}

TEST(BindingLayoutRuleTest, AContractD3D12VariantPasses) {
  auto variant = D3D12Variant(
      "cbuffer VertexUniformBlock : register(b0) { float4x4 m; };\n"
      "float4 main(float2 p : TEXCOORD0) : SV_Position { return 0; }\n",
      "cbuffer FragmentUniformBlock : register(b1) { float4 c; };\n"
      "Texture2D<float4> TextureSampler_0 : register(t0);\n"
      "SamplerState TextureSampler_0_sampler : register(s0);\n"
      "Texture2D<float4> TextureSampler_1 : register(t1);\n"
      "SamplerState TextureSampler_1_sampler : register(s1);\n"
      "float4 main() : SV_Target { return c; }\n");
  std::vector<VariantData> variants = {variant};
  EXPECT_EQ(CheckVariants(variants), 0u);
}

TEST(BindingLayoutRuleTest, AD3D12VertexStageWithoutACBufferPasses) {
  // A kernel whose vertex stage reads no uniform block declares no cbuffer at all, which is legal.
  auto variant = D3D12Variant("float4 main(float2 p : TEXCOORD0) : SV_Position { return 0; }\n",
                              "cbuffer FragmentUniformBlock : register(b1) { float4 c; };\n"
                              "float4 main() : SV_Target { return c; }\n");
  std::vector<VariantData> variants = {variant};
  EXPECT_EQ(CheckVariants(variants), 0u);
}

TEST(BindingLayoutRuleTest, AD3D12FragmentUboOnTheVertexRegisterFails) {
  // Both cbuffers at b0 would make the fragment root parameter address a register the bytecode
  // never reads: the pipeline uploads the uniform and the shader sees zeros.
  auto variant = D3D12Variant(
      "cbuffer VertexUniformBlock : register(b0) { float4x4 m; };\n"
      "float4 main(float2 p : TEXCOORD0) : SV_Position { return 0; }\n",
      "cbuffer FragmentUniformBlock : register(b0) { float4 c; };\n"
      "float4 main() : SV_Target { return c; }\n");
  std::vector<VariantData> variants = {variant};
  EXPECT_GE(CheckVariants(variants), 1u);
}

TEST(BindingLayoutRuleTest, AD3D12TextureWithoutAMatchingSamplerFails) {
  // The pipeline binds the SRV and the Sampler through two separate descriptor tables on the same
  // index; a sampler register that did not follow its texture register breaks that pairing.
  auto variant = D3D12Variant(
      "cbuffer VertexUniformBlock : register(b0) { float4x4 m; };\n"
      "float4 main(float2 p : TEXCOORD0) : SV_Position { return 0; }\n",
      "cbuffer FragmentUniformBlock : register(b1) { float4 c; };\n"
      "Texture2D<float4> TextureSampler_0 : register(t0);\n"
      "SamplerState TextureSampler_0_sampler : register(s1);\n"
      "float4 main() : SV_Target { return c; }\n");
  std::vector<VariantData> variants = {variant};
  EXPECT_GE(CheckVariants(variants), 1u);
}

TEST(BindingLayoutRuleTest, AD3D12StageWithoutAnEntryPointIsUnverifiable) {
  // A blob with no system-value output is not HLSL this rule can read: reported as unverifiable
  // instead of passing with zero registers found.
  auto variant = D3D12Variant("cbuffer VertexUniformBlock : register(b0) { float4x4 m; };\n",
                              "float4 main() : SV_Target { return 0; }\n");
  std::vector<VariantData> variants = {variant};
  Diagnostics diagnostics;
  auto errors = rule.check(ContextOf(variants, noExpansions), &diagnostics);
  EXPECT_EQ(errors, FailuresIn(diagnostics));
  EXPECT_GE(errors, 1u);
  ASSERT_FALSE(diagnostics.records().empty());
  EXPECT_EQ(diagnostics.records()[0].severity, Severity::Unverifiable);
  EXPECT_STREQ(diagnostics.records()[0].code, DiagnosticCode::VerifyNotApplicable);
  EXPECT_NE(diagnostics.records()[0].message.find("SV_Position"), std::string::npos);
}

TEST(BindingLayoutRuleTest, GlStorageIsScannedWhileMetalAndUnknownTagsAreSkipped) {
  // The GL/GLES storage is GLSL text: the rule scans it for a surviving set/binding qualifier and
  // the wrong dialect header. MSL carries only the binding numbers and an unparseable tag is
  // skipped the same way.
  std::vector<VariantData> variants;
  VariantData glVariant;
  glVariant.profileTag = "opengl";
  glVariant.vertexBlob = BytesOf("#version 330\nvoid main() {}\n");
  variants.push_back(glVariant);
  VariantData metalVariant;
  metalVariant.profileTag = "metal";
  metalVariant.vertexBlob = SpirvWithDecorations({{9, 9}});
  variants.push_back(metalVariant);
  VariantData unknownTag;
  unknownTag.profileTag = "nosuchprofile";
  unknownTag.vertexBlob = SpirvWithDecorations({{9, 9}});
  variants.push_back(unknownTag);
  EXPECT_EQ(CheckVariants(variants), 0u);
}

TEST(BindingLayoutRuleTest, GlStorageReportsASurvivingLayoutQualifier) {
  // The emitters strip the layout qualifiers; a survivor would hand the driver a set/binding the
  // runtime never binds, so the rule fails it.
  std::vector<VariantData> variants;
  VariantData glVariant;
  glVariant.profileTag = "opengles";
  glVariant.vertexBlob = BytesOf(
      "#version 300 es\nprecision mediump float;\nlayout(set = 0, binding = 0) uniform B { "
      "vec4 c; };\nvoid main() {}\n");
  variants.push_back(glVariant);
  Diagnostics diagnostics;
  auto errors = rule.check(ContextOf(variants, noExpansions), &diagnostics);
  EXPECT_EQ(errors, 1u);
  ASSERT_FALSE(diagnostics.records().empty());
  EXPECT_STREQ(diagnostics.records()[0].code, DiagnosticCode::EmitFormLeak);
}

// ---- A check that cannot run is reported, never skipped -------------------------------------

TEST(BindingLayoutRuleTest, AVertexStageWithoutAnEntryPointIsUnverifiable) {
  // The vertex slot scan locates the parameter list through "fn main("; a stage without one is
  // a scan that cannot run, and "could not run" must fail the build like a violation.
  auto variant = WebgpuVariant(WebgpuFragmentText(1), 1);
  variant.vertexBlob = BytesOf("@binding(0) @group(0) var<uniform> x : S;\n");
  std::vector<VariantData> variants = {variant};
  Diagnostics diagnostics;
  auto errors = rule.check(ContextOf(variants, noExpansions), &diagnostics);
  EXPECT_EQ(errors, FailuresIn(diagnostics));
  EXPECT_GE(errors, 1u);
  ASSERT_FALSE(diagnostics.records().empty());
  EXPECT_EQ(diagnostics.records()[0].severity, Severity::Unverifiable);
  EXPECT_STREQ(diagnostics.records()[0].code, DiagnosticCode::VerifyNotApplicable);
  EXPECT_NE(diagnostics.records()[0].message.find("fn main("), std::string::npos);
}

TEST(BindingLayoutRuleTest, AVertexStageWithoutQualifiersIsUnverifiable) {
  // The vertex stage always declares the vertex UBO, so an empty qualifier table means the scan
  // could not read the text — not that the stage declares nothing.
  auto variant = WebgpuVariant(WebgpuFragmentText(0), 0);
  variant.vertexBlob = BytesOf(
      "fn main(@location(0) pos : vec4<f32>) -> @builtin(position) vec4<f32> {\n"
      "  return pos;\n"
      "}\n");
  std::vector<VariantData> variants = {variant};
  Diagnostics diagnostics;
  auto errors = rule.check(ContextOf(variants, noExpansions), &diagnostics);
  EXPECT_EQ(errors, FailuresIn(diagnostics));
  EXPECT_GE(errors, 1u);
  ASSERT_FALSE(diagnostics.records().empty());
  EXPECT_EQ(diagnostics.records()[0].severity, Severity::Unverifiable);
  EXPECT_STREQ(diagnostics.records()[0].code, DiagnosticCode::VerifyNotApplicable);
}

TEST(BindingLayoutRuleTest, AnUnparseableVulkanModuleIsUnverifiable) {
  // A blob that is not SPIR-V (wrong magic, wrong size, truncated stream) leaves the Vulkan
  // binding check without input; reported as unverifiable instead of passing vacuously.
  VariantData variant;
  variant.shaderName = "TestShader";
  variant.profileTag = "vulkan";
  variant.vertexBlob = BytesOf("not a spir-v module");
  variant.fragmentBlob =
      SpirvWithDecorations({{FRAGMENT_UBO_DESCRIPTOR_SET, FRAGMENT_UBO_BINDING_POINT}});
  std::vector<VariantData> variants = {variant};
  Diagnostics diagnostics;
  auto errors = rule.check(ContextOf(variants, noExpansions), &diagnostics);
  EXPECT_EQ(errors, FailuresIn(diagnostics));
  EXPECT_GE(errors, 1u);
  ASSERT_FALSE(diagnostics.records().empty());
  EXPECT_EQ(diagnostics.records()[0].severity, Severity::Unverifiable);
  EXPECT_STREQ(diagnostics.records()[0].code, DiagnosticCode::VerifyNotApplicable);
}

}  // namespace tgfx

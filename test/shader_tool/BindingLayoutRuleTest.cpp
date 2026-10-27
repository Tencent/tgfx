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
#include "gpu/ShaderBindingLayout.h"
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

// A WebGPU variant whose stages spell the contract layout in tint's qualifier order.
VariantData WebgpuVariant(const std::string& fragText, size_t samplerCount) {
  VariantData variant;
  variant.shaderName = "TestShader";
  variant.profileTag = "webgpu";
  variant.vertexBlob = BytesOf("@binding(0) @group(0) var<uniform> x : S;\n");
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

}  // namespace

TEST(BindingLayoutRuleTest, AContractWebgpuVariantPasses) {
  std::vector<VariantData> variants = {WebgpuVariant(WebgpuFragmentText(2), 2)};
  EXPECT_EQ(rule.check(ContextOf(variants, noExpansions)), 0u);
}

TEST(BindingLayoutRuleTest, BothQualifierOrdersAreUnderstood) {
  // @group first in the text must read the same as @binding first: tint writes one order, but
  // the rule must not depend on it.
  VariantData groupFirst = WebgpuVariant(WebgpuFragmentText(1), 1);
  groupFirst.fragmentBlob = BytesOf(
      "@group(1) @binding(1) var<uniform> u : S;\n@group(2) @binding(0) var t0 : "
      "texture_2d<f32>;\n@group(2) @binding(1) var s0 : sampler;\n");
  std::vector<VariantData> variants = {groupFirst};
  EXPECT_EQ(rule.check(ContextOf(variants, noExpansions)), 0u);
}

TEST(BindingLayoutRuleTest, AWgslWithFewerInputsThanTheReflectionPasses) {
  // The shaderc performance pass legally drops vertex inputs the body never uses, so the WGSL
  // entry may declare fewer parameters than the reflection — here slot 4 is gone — as long as
  // every slot it does read is one the reflection (and so the runtime) covers.
  VariantData variant = WebgpuVariant(WebgpuFragmentText(1), 1);
  variant.vertexBlob = BytesOf(
      "@vertex\nfn main(@location(0) aPosition : vec2f, @location(1) inCoverage : f32, "
      "@location(2) inEllipseOffset : vec2f, @location(3) inEllipseRadii : vec2f, "
      "@location(5) inColor : vec4f) -> main_out {\n  return main_out();\n}\n");
  for (uint32_t location : {0u, 1u, 2u, 3u, 4u, 5u}) {
    VertexInputEntry input = {};
    input.location = static_cast<uint8_t>(location);
    variant.vertexReflection.vertexInputs.push_back(input);
  }
  std::vector<VariantData> variants = {variant};
  EXPECT_EQ(rule.check(ContextOf(variants, noExpansions)), 0u);
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
  EXPECT_GE(rule.check(ContextOf(variants, noExpansions)), 1u);
}

TEST(BindingLayoutRuleTest, AFragmentResourceInAGroupThePipelineNeverBindsFails) {
  auto text = WebgpuFragmentText(1) + "@binding(0) @group(3) var stray : f32;\n";
  std::vector<VariantData> variants = {WebgpuVariant(text, 1)};
  EXPECT_GE(rule.check(ContextOf(variants, noExpansions)), 1u);
}

TEST(BindingLayoutRuleTest, AFragmentUboOnTheWrongBindingFails) {
  VariantData variant = WebgpuVariant(WebgpuFragmentText(0), 0);
  variant.fragmentBlob = BytesOf("@binding(2) @group(1) var<uniform> u : S;\n");
  std::vector<VariantData> variants = {variant};
  EXPECT_GE(rule.check(ContextOf(variants, noExpansions)), 1u);
}

TEST(BindingLayoutRuleTest, AMissingTexturePairFails) {
  // Two samplers are reflected, but the text only carries one pair.
  std::vector<VariantData> variants = {WebgpuVariant(WebgpuFragmentText(1), 2)};
  EXPECT_GE(rule.check(ContextOf(variants, noExpansions)), 1u);
}

TEST(BindingLayoutRuleTest, AVertexResourceOutsideTheVertexSetFails) {
  VariantData variant = WebgpuVariant(WebgpuFragmentText(1), 1);
  variant.vertexBlob = BytesOf("@binding(0) @group(2) var<uniform> x : S;\n");
  std::vector<VariantData> variants = {variant};
  EXPECT_GE(rule.check(ContextOf(variants, noExpansions)), 1u);
}

TEST(BindingLayoutRuleTest, AContractVulkanVariantPasses) {
  VariantData variant;
  variant.shaderName = "TestShader";
  variant.profileTag = "vulkan";
  variant.vertexBlob = SpirvWithDecorations({{VERTEX_UBO_DESCRIPTOR_SET, VERTEX_UBO_BINDING_POINT}});
  variant.fragmentBlob = SpirvWithDecorations(
      {{FRAGMENT_UBO_DESCRIPTOR_SET, FRAGMENT_UBO_BINDING_POINT},
       {TEXTURE_DESCRIPTOR_SET, 0},
       {TEXTURE_DESCRIPTOR_SET, 1}});
  std::vector<VariantData> variants = {variant};
  EXPECT_EQ(rule.check(ContextOf(variants, noExpansions)), 0u);
}

TEST(BindingLayoutRuleTest, AVulkanFragmentResourceInSetZeroFails) {
  // The fragment stage of the pre-contract layout: the fragment UBO declared in the vertex
  // set, where the pipeline binds only the vertex UBO's descriptor set.
  VariantData variant;
  variant.shaderName = "TestShader";
  variant.profileTag = "vulkan";
  variant.vertexBlob = SpirvWithDecorations({{VERTEX_UBO_DESCRIPTOR_SET, VERTEX_UBO_BINDING_POINT}});
  variant.fragmentBlob = SpirvWithDecorations(
      {{VERTEX_UBO_DESCRIPTOR_SET, FRAGMENT_UBO_BINDING_POINT},
       {TEXTURE_DESCRIPTOR_SET, 0}});
  std::vector<VariantData> variants = {variant};
  EXPECT_GE(rule.check(ContextOf(variants, noExpansions)), 1u);
}

TEST(BindingLayoutRuleTest, AVulkanUboOnTheWrongBindingFails) {
  VariantData variant;
  variant.shaderName = "TestShader";
  variant.profileTag = "vulkan";
  variant.vertexBlob = SpirvWithDecorations({{VERTEX_UBO_DESCRIPTOR_SET, 3}});
  std::vector<VariantData> variants = {variant};
  EXPECT_GE(rule.check(ContextOf(variants, noExpansions)), 1u);
}

TEST(BindingLayoutRuleTest, VariantsOfUncheckedProfilesAreIgnored) {
  // The GL/GLES storage carries no set or binding and MSL only the binding numbers, so the
  // rule reads neither; an unparseable profile tag is skipped the same way.
  std::vector<VariantData> variants;
  VariantData glVariant;
  glVariant.profileTag = "opengl";
  glVariant.vertexBlob = SpirvWithDecorations({{9, 9}});
  variants.push_back(glVariant);
  VariantData unknownTag;
  unknownTag.profileTag = "nosuchprofile";
  unknownTag.vertexBlob = SpirvWithDecorations({{9, 9}});
  variants.push_back(unknownTag);
  EXPECT_EQ(rule.check(ContextOf(variants, noExpansions)), 0u);
}

}  // namespace tgfx

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

// The shader profile contract (src/gpu/shaders/contract/ShaderBundleContract.h, ShaderProfileResolver.h). The tag strings
// are stored in every bundle header and hashed into every stage key, so the literals below pin
// them independently of the implementation; they must not be regenerated from it.

#include <string>
#include "base/TGFXTest.h"
#include "gpu/aot/select/ShaderProfileResolver.h"
#include "gpu/shaders/Families.h"
#include "gpu/shaders/FamilyDomains.h"
#include "gpu/shaders/PrecompiledShader.h"
#include "gpu/shaders/contract/ShaderBundleContract.h"

namespace tgfx {

TGFX_TEST(ShaderProfileTest, TagsAreStable) {
  EXPECT_STREQ(ProfileTag(ShaderProfile::OpenGL), "opengl");
  EXPECT_STREQ(ProfileTag(ShaderProfile::OpenGLES), "opengles");
  EXPECT_STREQ(ProfileTag(ShaderProfile::Vulkan), "vulkan");
  EXPECT_STREQ(ProfileTag(ShaderProfile::Metal), "metal");
  EXPECT_STREQ(ProfileTag(ShaderProfile::WebGPU), "webgpu");
  EXPECT_STREQ(ProfileTag(ShaderProfile::D3D12), "d3d12");
}

TGFX_TEST(ShaderProfileTest, ParseRoundTripsEveryProfile) {
  size_t count = 0;
  for (auto profile : AllShaderProfiles) {
    auto parsed = ParseProfileTag(ProfileTag(profile));
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, profile);
    count++;
  }
  EXPECT_EQ(count, 6u);
}

TGFX_TEST(ShaderProfileTest, ParseRejectsUnknownTags) {
  EXPECT_FALSE(ParseProfileTag("").has_value());
  EXPECT_FALSE(ParseProfileTag("OpenGL").has_value());
  EXPECT_FALSE(ParseProfileTag("opengl ").has_value());
  EXPECT_FALSE(ParseProfileTag("vulkan-android").has_value());
  EXPECT_FALSE(ParseProfileTag("dx12").has_value());
}

TGFX_TEST(ShaderProfileTest, FramebufferFetchSupport) {
  EXPECT_TRUE(ProfileSupportsFramebufferFetch(ShaderProfile::Metal));
  EXPECT_TRUE(ProfileSupportsFramebufferFetch(ShaderProfile::OpenGLES));
  EXPECT_FALSE(ProfileSupportsFramebufferFetch(ShaderProfile::OpenGL));
  EXPECT_FALSE(ProfileSupportsFramebufferFetch(ShaderProfile::Vulkan));
  EXPECT_FALSE(ProfileSupportsFramebufferFetch(ShaderProfile::WebGPU));
  EXPECT_FALSE(ProfileSupportsFramebufferFetch(ShaderProfile::D3D12));
}

TGFX_TEST(ShaderProfileTest, PayloadFormsFollowTheProfileRule) {
  // Vulkan stores SPIR-V words and the GL/WGSL profiles store driver source; only Metal and D3D12
  // support both a precompiled container and the source their driver compiles.
  for (auto profile : AllShaderProfiles) {
    SCOPED_TRACE(ProfileTag(profile));
    switch (profile) {
      case ShaderProfile::Vulkan:
        EXPECT_FALSE(ProfileAllowsPayload(profile, BundleFormat::Payload::Text));
        EXPECT_TRUE(ProfileAllowsPayload(profile, BundleFormat::Payload::Binary));
        break;
      case ShaderProfile::Metal:
      case ShaderProfile::D3D12:
        EXPECT_TRUE(ProfileAllowsPayload(profile, BundleFormat::Payload::Text));
        EXPECT_TRUE(ProfileAllowsPayload(profile, BundleFormat::Payload::Binary));
        break;
      case ShaderProfile::OpenGL:
      case ShaderProfile::OpenGLES:
      case ShaderProfile::WebGPU:
        EXPECT_TRUE(ProfileAllowsPayload(profile, BundleFormat::Payload::Text));
        EXPECT_FALSE(ProfileAllowsPayload(profile, BundleFormat::Payload::Binary));
        break;
    }
    // A value past the last defined form names nothing the runtime could pick.
    EXPECT_FALSE(ProfileAllowsPayload(profile, BundleFormat::Payload::Max + 1));
  }
}

TGFX_TEST(ShaderProfileTest, FramebufferFetchVariantsFollowTheProfileRule) {
  // The declared Chain family carries the HAS_XP dim (the legacy YUV shell retired).
  const auto chainIndex = FamilyIndexOf("Chain");
  ASSERT_NE(chainIndex, 0xFFFF);
  const auto& family = Families()[chainIndex];
  const auto variants = EnumerateVariants(family, chainIndex);
  size_t checked = 0;
  for (const auto& variant : variants) {
    const bool fetch = VariantUsesFramebufferFetch(chainIndex, variant.ordinal);
    for (auto profile : AllShaderProfiles) {
      const bool compiles = PermutationCompilesForBackend(chainIndex, variant.ordinal, profile);
      EXPECT_EQ(compiles, !fetch || ProfileSupportsFramebufferFetch(profile))
          << ProfileTag(profile) << " ordinal " << variant.ordinal;
      checked++;
    }
  }
  EXPECT_GT(checked, 0u);
}

TGFX_TEST(ShaderProfileTest, BackendToProfile) {
  EXPECT_EQ(ExpectedShaderProfile(Backend::OpenGL), ShaderProfile::OpenGL);
  EXPECT_EQ(ExpectedShaderProfile(Backend::Metal), ShaderProfile::Metal);
  EXPECT_EQ(ExpectedShaderProfile(Backend::Vulkan), ShaderProfile::Vulkan);
  EXPECT_EQ(ExpectedShaderProfile(Backend::WebGPU), ShaderProfile::WebGPU);
  EXPECT_EQ(ExpectedShaderProfile(Backend::D3D12), ShaderProfile::D3D12);
  EXPECT_FALSE(ExpectedShaderProfile(Backend::Unknown).has_value());
}

TGFX_TEST(ShaderProfileTest, OpenGLProfileFollowsPrecisionModifiers) {
  EXPECT_EQ(ResolveGLShaderProfile(true), ShaderProfile::OpenGLES);
  EXPECT_EQ(ResolveGLShaderProfile(false), ShaderProfile::OpenGL);
}

// A context is served by exactly one bundle profile: an ES context must never accept the desktop
// bundle, because loading it makes every draw fail the match-time GL gate (a silent black frame).
TGFX_TEST(ShaderProfileTest, RequiredProfileFollowsTheContext) {
  EXPECT_EQ(RequiredProfile(ShaderProfile::OpenGL, true), ShaderProfile::OpenGLES);
  EXPECT_EQ(RequiredProfile(ShaderProfile::OpenGL, false), ShaderProfile::OpenGL);
  EXPECT_EQ(RequiredProfile(ShaderProfile::Metal, true), ShaderProfile::Metal);
  EXPECT_EQ(RequiredProfile(ShaderProfile::Vulkan, false), ShaderProfile::Vulkan);
  // The tag of the rejected bundle is not the required one.
  EXPECT_STRNE(ProfileTag(RequiredProfile(ShaderProfile::OpenGL, true)), "opengl");
}

}  // namespace tgfx

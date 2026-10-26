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

// The shader profile contract (src/gpu/ShaderProfile.h, ShaderProfileResolver.h). The tag strings
// are stored in every bundle header and hashed into every stage key, so the literals below pin
// them independently of the implementation; they must not be regenerated from it.

#include <string>
#include "base/TGFXTest.h"
#include "gpu/ShaderProfile.h"
#include "gpu/ShaderProfileResolver.h"
#include "gpu/shaders/PrecompiledShader.h"
#include "gpu/shaders/level1/QuadTextureFillShader.h"

namespace tgfx {

TGFX_TEST(ShaderProfileTest, TagsAreStable) {
  EXPECT_STREQ(ProfileTag(ShaderProfile::OpenGL), "opengl");
  EXPECT_STREQ(ProfileTag(ShaderProfile::OpenGLES), "opengles");
  EXPECT_STREQ(ProfileTag(ShaderProfile::Vulkan), "vulkan");
  EXPECT_STREQ(ProfileTag(ShaderProfile::Metal), "metal");
  EXPECT_STREQ(ProfileTag(ShaderProfile::WebGPU), "webgpu");
}

TGFX_TEST(ShaderProfileTest, ParseRoundTripsEveryProfile) {
  size_t count = 0;
  for (auto profile : AllShaderProfiles) {
    auto parsed = ParseProfileTag(ProfileTag(profile));
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, profile);
    count++;
  }
  EXPECT_EQ(count, 5u);
}

TGFX_TEST(ShaderProfileTest, ParseRejectsUnknownTags) {
  EXPECT_FALSE(ParseProfileTag("").has_value());
  EXPECT_FALSE(ParseProfileTag("OpenGL").has_value());
  EXPECT_FALSE(ParseProfileTag("opengl ").has_value());
  EXPECT_FALSE(ParseProfileTag("vulkan-android").has_value());
  EXPECT_FALSE(ParseProfileTag("d3d12").has_value());
}

TGFX_TEST(ShaderProfileTest, FramebufferFetchSupport) {
  EXPECT_TRUE(ProfileSupportsFramebufferFetch(ShaderProfile::Metal));
  EXPECT_TRUE(ProfileSupportsFramebufferFetch(ShaderProfile::OpenGLES));
  EXPECT_FALSE(ProfileSupportsFramebufferFetch(ShaderProfile::OpenGL));
  EXPECT_FALSE(ProfileSupportsFramebufferFetch(ShaderProfile::Vulkan));
  EXPECT_FALSE(ProfileSupportsFramebufferFetch(ShaderProfile::WebGPU));
}

TGFX_TEST(ShaderProfileTest, FramebufferFetchVariantsFollowTheProfileRule) {
  auto info = QuadTextureFillShader().info();
  const auto& fragDomain = info.fragDomain;
  size_t checked = 0;
  for (uint32_t fragIndex = 0; fragIndex < fragDomain.totalCount(); ++fragIndex) {
    bool fetch = fragDomain.valueOf(fragIndex, XP_DEFINE_NAME) == XP_FRAMEBUFFER_FETCH;
    for (auto profile : AllShaderProfiles) {
      bool compiles = PermutationCompilesForBackend(info, 0, fragIndex, profile);
      EXPECT_EQ(compiles, !fetch || ProfileSupportsFramebufferFetch(profile))
          << ProfileTag(profile) << " frag " << fragIndex;
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
  EXPECT_FALSE(ExpectedShaderProfile(Backend::D3D12).has_value());
  EXPECT_FALSE(ExpectedShaderProfile(Backend::Unknown).has_value());
}

TGFX_TEST(ShaderProfileTest, OpenGLProfileFollowsPrecisionModifiers) {
  EXPECT_EQ(ResolveGLShaderProfile(true), ShaderProfile::OpenGLES);
  EXPECT_EQ(ResolveGLShaderProfile(false), ShaderProfile::OpenGL);
}

}  // namespace tgfx

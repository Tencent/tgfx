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
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "../../tools/glsl_to_usf/GlslRectNormalizer.h"
#include "gtest/gtest.h"

namespace tgfx {

// Case 1 — TextureEffect_RGBA with a rect sampler: declaration must split into sampler2D +
// inverse-size companion; the call-site coord arg must pick up the multiplier.
TEST(GlslRectNormalizerTest, TextureEffectRGBA_WrapsCoordArg) {
  std::string src = R"(#version 150
uniform sampler2DRect TextureSampler_0_P1;
in vec2 TransformedCoords_0_P0;
out vec4 tgfx_FragColor;

vec4 TGFX_TextureEffect_RGBA(vec4 i, vec2 c, sampler2D s, vec4 a, vec4 b, vec2 d,
                              int e, int f, int g, int h) { return texture(s, c); }
vec4 TGFX_TextureEffect_RGBA(vec4 i, vec2 c, sampler2DRect s, vec4 a, vec4 b, vec2 d,
                              int e, int f, int g, int h) { return texture(s, c); }

void main() {
  tgfx_FragColor = TGFX_TextureEffect_RGBA(vec4(1.0), TransformedCoords_0_P0,
                                           TextureSampler_0_P1, vec4(0.0), vec4(0.0),
                                           vec2(0.0), 0, 0, 0, 0);
}
)";
  auto result = NormalizeRectSamplers(src, GlslStage::Fragment);
  ASSERT_TRUE(result.errorMessage.empty()) << result.errorMessage;
  EXPECT_NE(result.glsl.find("uniform sampler2D TextureSampler_0_P1;"), std::string::npos);
  EXPECT_NE(result.glsl.find("layout(std140) uniform TgfxRectFragmentUniformBlock"),
            std::string::npos);
  EXPECT_NE(result.glsl.find("vec2 TGFX_InvRectSize_TextureSampler_0_P1;"), std::string::npos);
  EXPECT_EQ(result.glsl.find("sampler2DRect"), std::string::npos)
      << "All sampler2DRect occurrences should be rewritten or stripped";
  EXPECT_NE(result.glsl.find("(TransformedCoords_0_P0) * TGFX_InvRectSize_TextureSampler_0_P1"),
            std::string::npos);
  ASSERT_EQ(result.rects.size(), 1u);
  EXPECT_EQ(result.rects[0].name, "TextureSampler_0_P1");
  EXPECT_EQ(result.rects[0].invSizeUniform, "TGFX_InvRectSize_TextureSampler_0_P1");
}

// Case 2 — TiledTextureEffect shares the same signature shape as TextureEffect_RGBA.
TEST(GlslRectNormalizerTest, TiledTextureEffect_WrapsCoordArg) {
  std::string src = R"(#version 150
uniform sampler2DRect TextureSampler_0_P1;
in vec2 TransformedCoords_0_P0;
out vec4 tgfx_FragColor;
vec4 TGFX_TiledTextureEffect(vec4 i, vec2 c, sampler2D s, int x) { return texture(s, c); }
vec4 TGFX_TiledTextureEffect(vec4 i, vec2 c, sampler2DRect s, int x) { return texture(s, c); }
void main() {
  tgfx_FragColor = TGFX_TiledTextureEffect(vec4(1.0), TransformedCoords_0_P0,
                                            TextureSampler_0_P1, 0);
}
)";
  auto result = NormalizeRectSamplers(src, GlslStage::Fragment);
  ASSERT_TRUE(result.errorMessage.empty()) << result.errorMessage;
  EXPECT_NE(result.glsl.find("(TransformedCoords_0_P0) * TGFX_InvRectSize_TextureSampler_0_P1"),
            std::string::npos);
  EXPECT_EQ(result.glsl.find("sampler2DRect"), std::string::npos);
}

// Case 3 — AtlasText_SampleAtlas has the sampler at position 0 and the coord at position 1.
TEST(GlslRectNormalizerTest, AtlasTextSampleAtlas_WrapsCoordArg) {
  std::string src = R"(#version 150
uniform sampler2DRect TextureSampler_P0;
in vec2 textureCoords_P0;
out vec4 tgfx_FragColor;
vec4 TGFX_AtlasText_SampleAtlas(sampler2D tex, vec2 uv) { return texture(tex, uv); }
vec4 TGFX_AtlasText_SampleAtlas(sampler2DRect tex, vec2 uv) { return texture(tex, uv); }
void main() {
  tgfx_FragColor = TGFX_AtlasText_SampleAtlas(TextureSampler_P0, textureCoords_P0);
}
)";
  auto result = NormalizeRectSamplers(src, GlslStage::Fragment);
  ASSERT_TRUE(result.errorMessage.empty()) << result.errorMessage;
  EXPECT_NE(result.glsl.find("TGFX_AtlasText_SampleAtlas(TextureSampler_P0, "
                             "(textureCoords_P0) * TGFX_InvRectSize_TextureSampler_P0)"),
            std::string::npos);
  EXPECT_EQ(result.glsl.find("sampler2DRect"), std::string::npos);
}

// Case 4 — PorterDuffXP: declaration must flip to sampler2D and the companion uniform is
// emitted, but the call-site is NOT wrapped (PorterDuff consumes DstTextureCoordScale_Pn
// supplied by UE as 1/(w,h), so the coord is already normalized inside the helper body).
TEST(GlslRectNormalizerTest, PorterDuff_DeclarationOnly_NoCallSiteWrap) {
  std::string src = R"(#version 150
uniform sampler2DRect DstTextureSampler_P2;
out vec4 tgfx_FragColor;
void TGFX_PorterDuffXP_FS(vec4 i, vec4 c, vec2 u, vec2 s, sampler2D t, out vec4 o) {
  o = texture(t, vec2(0.0));
}
void TGFX_PorterDuffXP_FS(vec4 i, vec4 c, vec2 u, vec2 s, sampler2DRect t, out vec4 o) {
  o = texture(t, vec2(0.0));
}
void main() {
  TGFX_PorterDuffXP_FS(vec4(1.0), vec4(1.0), vec2(0.0), vec2(1.0),
                        DstTextureSampler_P2, tgfx_FragColor);
}
)";
  auto result = NormalizeRectSamplers(src, GlslStage::Fragment);
  ASSERT_TRUE(result.errorMessage.empty()) << result.errorMessage;
  EXPECT_NE(result.glsl.find("uniform sampler2D DstTextureSampler_P2;"), std::string::npos);
  EXPECT_NE(result.glsl.find("vec2 TGFX_InvRectSize_DstTextureSampler_P2;"), std::string::npos);
  EXPECT_EQ(result.glsl.find("* TGFX_InvRectSize_DstTextureSampler_P2"), std::string::npos)
      << "PorterDuff call sites must not be wrapped";
  EXPECT_EQ(result.glsl.find("sampler2DRect"), std::string::npos);
  ASSERT_EQ(result.rects.size(), 1u);
  EXPECT_EQ(result.rects[0].name, "DstTextureSampler_P2");
}

// Case 5 — two independent rect uniforms in the same shader, each rewritten.
TEST(GlslRectNormalizerTest, TwoRectSamplers_BothRewritten) {
  std::string src = R"(#version 150
uniform sampler2DRect TextureSampler_0_P1;
uniform sampler2DRect TextureSampler_1_P1;
in vec2 c0;
in vec2 c1;
out vec4 tgfx_FragColor;
vec4 TGFX_TextureEffect_RGBA(vec4 i, vec2 c, sampler2D s, vec4 a, vec4 b, vec2 d,
                              int e, int f, int g, int h) { return texture(s, c); }
vec4 TGFX_TextureEffect_RGBA(vec4 i, vec2 c, sampler2DRect s, vec4 a, vec4 b, vec2 d,
                              int e, int f, int g, int h) { return texture(s, c); }
void main() {
  vec4 x = TGFX_TextureEffect_RGBA(vec4(1.0), c0, TextureSampler_0_P1,
                                    vec4(0.0), vec4(0.0), vec2(0.0), 0, 0, 0, 0);
  vec4 y = TGFX_TextureEffect_RGBA(vec4(1.0), c1, TextureSampler_1_P1,
                                    vec4(0.0), vec4(0.0), vec2(0.0), 0, 0, 0, 0);
  tgfx_FragColor = x + y;
}
)";
  auto result = NormalizeRectSamplers(src, GlslStage::Fragment);
  ASSERT_TRUE(result.errorMessage.empty()) << result.errorMessage;
  EXPECT_NE(result.glsl.find("(c0) * TGFX_InvRectSize_TextureSampler_0_P1"), std::string::npos);
  EXPECT_NE(result.glsl.find("(c1) * TGFX_InvRectSize_TextureSampler_1_P1"), std::string::npos);
  ASSERT_EQ(result.rects.size(), 2u);
  // Rects are returned in sorted order for manifest stability.
  EXPECT_EQ(result.rects[0].name, "TextureSampler_0_P1");
  EXPECT_EQ(result.rects[1].name, "TextureSampler_1_P1");
}

// Case 6 — no rect uniform in the source: output is byte-identical to input.
TEST(GlslRectNormalizerTest, NoRect_PassThroughByteIdentical) {
  std::string src = R"(#version 150
uniform sampler2D TextureSampler_0_P1;
in vec2 c;
out vec4 tgfx_FragColor;
vec4 TGFX_TextureEffect_RGBA(vec4 i, vec2 x, sampler2D s, vec4 a, vec4 b, vec2 d,
                              int e, int f, int g, int h) { return texture(s, x); }
void main() {
  tgfx_FragColor = TGFX_TextureEffect_RGBA(vec4(1.0), c, TextureSampler_0_P1,
                                            vec4(0.0), vec4(0.0), vec2(0.0), 0, 0, 0, 0);
}
)";
  auto result = NormalizeRectSamplers(src, GlslStage::Fragment);
  ASSERT_TRUE(result.errorMessage.empty()) << result.errorMessage;
  EXPECT_TRUE(result.rects.empty());
  EXPECT_EQ(result.glsl, src);
}

// Case 7 — mixed rect + non-rect sampler arguments: only the call whose sampler is a rect
// uniform should be wrapped. The non-rect call's coord argument must not acquire a multiplier.
TEST(GlslRectNormalizerTest, MixedSamplers_OnlyRectCallWrapped) {
  std::string src = R"(#version 150
uniform sampler2DRect TextureSampler_0_P1;
uniform sampler2D TextureSampler_0_P2;
in vec2 c0;
in vec2 c1;
out vec4 tgfx_FragColor;
vec4 TGFX_TextureEffect_RGBA(vec4 i, vec2 c, sampler2D s, vec4 a, vec4 b, vec2 d,
                              int e, int f, int g, int h) { return texture(s, c); }
vec4 TGFX_TextureEffect_RGBA(vec4 i, vec2 c, sampler2DRect s, vec4 a, vec4 b, vec2 d,
                              int e, int f, int g, int h) { return texture(s, c); }
void main() {
  vec4 x = TGFX_TextureEffect_RGBA(vec4(1.0), c0, TextureSampler_0_P1,
                                    vec4(0.0), vec4(0.0), vec2(0.0), 0, 0, 0, 0);
  vec4 y = TGFX_TextureEffect_RGBA(vec4(1.0), c1, TextureSampler_0_P2,
                                    vec4(0.0), vec4(0.0), vec2(0.0), 0, 0, 0, 0);
  tgfx_FragColor = x + y;
}
)";
  auto result = NormalizeRectSamplers(src, GlslStage::Fragment);
  ASSERT_TRUE(result.errorMessage.empty()) << result.errorMessage;
  EXPECT_NE(result.glsl.find("(c0) * TGFX_InvRectSize_TextureSampler_0_P1"), std::string::npos);
  EXPECT_EQ(result.glsl.find("(c1) * TGFX_InvRectSize_TextureSampler_0_P2"), std::string::npos)
      << "The non-rect sampler's call site must not be wrapped";
  EXPECT_EQ(result.glsl.find("TGFX_InvRectSize_TextureSampler_0_P2"), std::string::npos);
}

// Case 8 — GBlur1D `#define` macro: rewrite the macro body once; the call-site expansion of
// the macro is not modified because the preprocessor propagates the rewrite.
TEST(GlslRectNormalizerTest, GB1DMacro_RewritesMacroBodyOnly) {
  std::string src = R"(#version 150
uniform sampler2DRect TextureSampler_0_P1;
in vec2 texCoord;
out vec4 tgfx_FragColor;
#define TGFX_GB1D_SAMPLE(coord) texture(TextureSampler_0_P1, coord)
void main() {
  vec4 sum = vec4(0.0);
  sum += TGFX_GB1D_SAMPLE(texCoord) * 0.5;
  tgfx_FragColor = sum;
}
)";
  auto result = NormalizeRectSamplers(src, GlslStage::Fragment);
  ASSERT_TRUE(result.errorMessage.empty()) << result.errorMessage;
  EXPECT_NE(result.glsl.find("#define TGFX_GB1D_SAMPLE(coord) texture(TextureSampler_0_P1, "
                             "(coord) * TGFX_InvRectSize_TextureSampler_0_P1)"),
            std::string::npos);
  // Call-site expansion is untouched — preprocessor will do the work.
  EXPECT_NE(result.glsl.find("TGFX_GB1D_SAMPLE(texCoord) * 0.5"), std::string::npos);
  ASSERT_EQ(result.rects.size(), 1u);
}

// Structural guard: an unrecognized `#define` that names a rect uniform must surface as an
// error so future emitter changes don't silently slip past.
TEST(GlslRectNormalizerTest, UnknownMacroReferencingRectUniform_ReportsError) {
  std::string src = R"(#version 150
uniform sampler2DRect TextureSampler_0_P1;
#define TGFX_CUSTOM_HACK(x) texture(TextureSampler_0_P1, x * 2.0)
void main() {}
)";
  auto result = NormalizeRectSamplers(src, GlslStage::Fragment);
  EXPECT_FALSE(result.errorMessage.empty())
      << "Expected normalizer to flag an unrecognized macro shape";
  EXPECT_NE(result.errorMessage.find("unrecognized #define"), std::string::npos);
}

// Vertex stage is always a pass-through — rect uniforms never appear in VS dumps, and running
// the normalizer over a VS source must not touch it.
TEST(GlslRectNormalizerTest, VertexStage_PassThrough) {
  std::string src = R"(#version 150
in vec2 aPosition;
uniform vec4 tgfx_RTAdjust;
void main() {
  gl_Position = vec4(aPosition, 0.0, 1.0) * vec4(tgfx_RTAdjust.xy, 1.0, 1.0);
}
)";
  auto result = NormalizeRectSamplers(src, GlslStage::Vertex);
  ASSERT_TRUE(result.errorMessage.empty()) << result.errorMessage;
  EXPECT_EQ(result.glsl, src);
  EXPECT_TRUE(result.rects.empty());
}

}  // namespace tgfx

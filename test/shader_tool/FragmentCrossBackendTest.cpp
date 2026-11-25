/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions and
//  limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

// The fragment-form gate: every op fragment is a normal function with typed parameters, and the
// build must be able to translate it to every backend. Each case wraps the fragment in a harness
// (the entry-level declarations stubbed the way the runtime's own would be), compiles it to SPIR-V
// with the real shaderc, then translates the module to HLSL and MSL via spirv-cross and to WGSL
// via tint. A signature the translators cannot express — an out parameter, a sampler argument, an
// array in the wrong address space — fails here instead of on a device.

#include <iostream>
#include <string>
#include <vector>
#include "compile/ShaderCompiler.h"
#include "compile/StageCompiler.h"
#include "core/ShaderSources.h"
#include "gtest/gtest.h"

namespace tgfx {
namespace {

const std::string FragmentShaderDir = std::string(TGFX_SOURCE_DIR) + "/src/gpu/shaders/glsl";

// The entry-level stubs every harness declares: one uniform block holding the pointwise
// parameter slots and the chain-wide pool, plus the input varyings and the output.
const char* const HarnessPrelude =
    "#version 450\n"
    "precision highp float;\n"
    "precision highp int;\n"
    "layout(set = 0, binding = 0, std140) uniform FragUniforms {\n"
    "  mat4 UMat4;\n"
    "  mat3 UMat3;\n"
    "  vec4 UVec4;\n"
    "  vec4 UVec4B;\n"
    "  vec4 UVec4C;\n"
    "  vec4 UVec4D;\n"
    "  float UFloat;\n"
    "  vec4 ChainData[144];\n"
    "  vec4 SourceCoords;\n"
    "  ivec4 NoiseModeOctaves;\n"
    "  int GradientLayoutType;\n"
    "  int GradientColorizerKind;\n"
    "  int GradientLUTLeaf;\n"
    "  int ChainCSFlags;\n"
    "  vec4 ChainSrcTF0;\n"
    "  vec4 ChainSrcTF1;\n"
    "  int ChainSrcTFType;\n"
    "  vec4 ChainSrcOOTF;\n"
    "  mat3 ChainColorXform;\n"
    "  vec4 ChainDstOOTF;\n"
    "  vec4 ChainDstTF0;\n"
    "  vec4 ChainDstTF1;\n"
    "  int ChainDstTFType;\n"
    "  vec4 Gradient_leftBorder;\n"
    "  vec4 Gradient_rightBorder;\n"
    "  vec4 Gradient_start;\n"
    "  vec4 Gradient_end;\n"
    "  vec4 Gradient_scale01;\n"
    "  vec4 Gradient_bias01;\n"
    "  vec4 Gradient_scale23;\n"
    "  vec4 Gradient_bias23;\n"
    "  float Gradient_threshold;\n"
    "  float Gradient_bias;\n"
    "  float Gradient_scale;\n"
    "  vec4 Gradient_thresholds1_7;\n"
    "  vec4 Gradient_thresholds9_13;\n"
    "  vec4 Gradient_scales[8];\n"
    "  vec4 Gradient_biases[8];\n"
    "  vec4 Noise_frequencyStitch;\n"
    "};\n"
    "layout(set = 0, binding = 1) uniform sampler2D TextureSampler_0;\n"
    "layout(set = 0, binding = 2) uniform sampler2D TextureSampler_1;\n"
    "layout(set = 0, binding = 3) uniform sampler2D TextureSampler_2;\n"
    "layout(set = 0, binding = 4) uniform sampler2D TextureSampler_3;\n"
    "layout(location = 0) in vec4 vColor;\n"
    "layout(location = 1) in float vCoverage;\n"
    "layout(location = 2) in float vEdge;\n"
    "layout(location = 0) out vec4 fragColor;\n";

struct FragmentCase {
  const char* name;
  const char* sourceFile;
  const char* defines;
  const char* call;
};

const FragmentCase Cases[] = {
    {"ColorMatrix", "ops/ColorMatrix.glsl", "",
     "fragColor = pmApplyColorMatrix(vColor, UMat4, UVec4);"},
    {"Luma", "ops/Luma.glsl", "", "fragColor = pmApplyLuma(vColor, UVec4.x, UVec4.y, UVec4.z);"},
    {"AlphaThreshold", "ops/AlphaThreshold.glsl", "",
     "fragColor = pmApplyAlphaThreshold(vColor, UFloat);"},
    {"ConstColor", "ops/ConstColor.glsl", "", "fragColor = pmApplyConstColor(UVec4, vColor, 1);"},
    {"AARectCoverage", "ops/AARectCoverage.glsl", "",
     "fragColor = tgfxOpAARectCoverage(vColor, UVec4, gl_FragCoord.xy);"},
    {"Texture", "ops/Texture.glsl", "", "fragColor = tgfxOpTexture(vColor, UVec4, UVec4B, 1);"},
    {"TexModulate", "ops/TexModulate.glsl", "", "fragColor = tgfxOpTexModulate(vColor, UVec4, 1);"},
    {"LocalRectCoverage", "ops/LocalRectCoverage.glsl", "",
     "fragColor = tgfxOpLocalRectCoverage(vColor, UVec4, UMat3, gl_FragCoord.xy);"},
    {"RRectCoverage", "ops/RRectCoverage.glsl", "",
     "fragColor = tgfxOpRRectCoverage(vColor, UVec4, UVec4B, UVec4C, UFloat, UMat3, "
     "gl_FragCoord.xy);"},
    {"ColorSpaceXform", "ops/ColorSpaceXform.glsl", "",
     "fragColor = chainApplyColorSpaceXform(vColor, 0, UVec4, UVec4B, 0, UVec4C, UMat3, "
     "UVec4D, UVec4, UVec4B, 0);"},
    {"Gradient", "pipeline/chain/pointwise_gradient.inc",
     "#define NTEX 1\n#define TGFX_DECLARED_CHAIN 1\n", "fragColor = evalGradientSlot(vColor);"},
    {"PerlinNoise", "pipeline/chain/pointwise_noise.inc", "#define TGFX_DECLARED_CHAIN 1\n",
     "fragColor = evalNoiseSlot();"},
};

// Reads a fragment source, expands its includes, and wraps it with the harness prelude and a main
// that calls it: the exact recipe the build's fragment packager will run.
std::string HarnessText(const FragmentCase& fragmentCase) {
  const std::string path = FragmentShaderDir + "/" + fragmentCase.sourceFile;
  std::string source;
  {
    std::string error;
    source = ExpandStageIncludes("#include \"" + std::string(fragmentCase.sourceFile) + "\"\n",
                                 FragmentShaderDir, &error);
    if (source.empty()) {
      ADD_FAILURE() << fragmentCase.name << ": " << error;
      return {};
    }
  }
  // #version must lead: the fragment's conditionals come after the harness prelude, and the
  // shared helpers the fragments call arrive through the same include expansion the build runs.
  std::string helpers;
  {
    std::string error;
    helpers = ExpandStageIncludes(
        "#include \"source/texture_sampling.inc\"\n"
        "#include \"coverage/analytic_clip_math.inc\"\n",
        FragmentShaderDir, &error);
    if (helpers.empty()) {
      ADD_FAILURE() << fragmentCase.name << ": " << error;
      return {};
    }
  }
  std::string text = HarnessPrelude;
  text += fragmentCase.defines;
  text += helpers;
  text += source;
  text += "\nvoid main() {\n  ";
  text += fragmentCase.call;
  text += "\n}\n";
  return text;
}

// The six-stage gate: shaderc for SPIR-V, spirv-cross for HLSL/MSL, tint for WGSL.
void MustTranslateEverywhere(const FragmentCase& fragmentCase) {
  const std::string text = HarnessText(fragmentCase);
  ASSERT_FALSE(text.empty()) << fragmentCase.name;
  std::string error;
  const auto spirv =
      CompileGLSL(text, ShaderStageType::Fragment, fragmentCase.name, 0, false, &error);
  ASSERT_FALSE(spirv.empty()) << fragmentCase.name << " (GLSL): " << error;

  const auto msl = TranslateToMSL(spirv, ShaderStageType::Fragment, &error);
  EXPECT_FALSE(msl.empty()) << fragmentCase.name << " (MSL): " << error;

  const auto hlsl = TranslateToHLSL(spirv, ShaderStageType::Fragment, &error);
  EXPECT_FALSE(hlsl.empty()) << fragmentCase.name << " (HLSL): " << error;

  const auto wgsl =
      CompileGLSLToWGSL(text, ShaderStageType::Fragment, fragmentCase.name, 0, &error);
  if (error.find("WebGPU translation is unavailable") != std::string::npos) {
    // This build did not enable the WebGPU bundle (TGFX_BUILD_WEBGPU_BUNDLE): the tint stage is
    // compiled out, so the WGSL leg is skipped here and exercised by the WebGPU configuration.
    std::cout << "[  SKIP  ] " << fragmentCase.name << " (WGSL): " << error << "\n";
    return;
  }
  EXPECT_FALSE(wgsl.empty()) << fragmentCase.name << " (WGSL): " << error;
}

}  // namespace

TEST(FragmentCrossBackendTest, EveryFragmentTranslatesToEveryBackend) {
  for (const auto& fragmentCase : Cases) {
    MustTranslateEverywhere(fragmentCase);
  }
}

}  // namespace tgfx

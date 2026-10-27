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

// The compile pipeline of the tool: text preparation (defines, preprocessing), GLSL to SPIR-V, and
// the two analyses that read the SPIR-V (reflection and inline expansion). The tests compile small
// GLSL programs with the real shaderc and check what the analyses make of them, so they cover the
// same path a build takes without depending on the shader sources of the project.

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "InlineExpansion.h"
#include "ReflectionExtractor.h"
#include "ShaderCompiler.h"
#include "compile/StageCompiler.h"
#include "gpu/ShaderBindingLayout.h"
#include "gpu/Uniform.h"
#include "gtest/gtest.h"

namespace tgfx {
namespace {

// The vertex shader every reflection test pairs with: it declares the vertex uniform block and two
// inputs, with a gap between their locations like the shape-instanced kernels have.
const char VertexSource[] = R"(#version 450
layout(location = 0) in vec2 aPosition;
layout(location = 2) in float aCoverage;
layout(std140, set = 0, binding = 0) uniform VertexUniformBlock {
  mat3 Matrix;
  vec2 Offset;
};
layout(location = 0) out float vCoverage;
void main() {
  vec3 position = Matrix * vec3(aPosition + Offset, 1.0);
  vCoverage = aCoverage;
  gl_Position = vec4(position.xy, 0.0, 1.0);
}
)";

// Samplers are declared out of binding order on purpose: the extractor must sort by binding.
const char FragmentSource[] = R"(#version 450
layout(location = 0) in float vCoverage;
layout(location = 0) out vec4 fragColor;
layout(std140, set = 0, binding = 1) uniform FragmentUniformBlock {
  vec4 Color;
  vec4 Stops[4];
  int Mode;
};
layout(set = 1, binding = 1) uniform sampler2D TextureSampler_1;
layout(set = 1, binding = 0) uniform sampler2D TextureSampler_0;
void main() {
  vec4 sampled = texture(TextureSampler_0, vec2(0.5)) + texture(TextureSampler_1, vec2(0.5));
  fragColor = Color * Stops[Mode] * sampled * vCoverage;
}
)";

std::vector<uint32_t> MustCompile(const std::string& source, ShaderStageType stage) {
  std::string error;
  auto spirv = CompileGLSL(source, stage, "TestShader", 0, false, &error);
  EXPECT_FALSE(spirv.empty()) << error;
  return spirv;
}

bool HasEntry(const std::vector<UniformEntry>& entries, const std::string& name, UniformFormat format,
              uint16_t arraySize = 1) {
  for (const auto& entry : entries) {
    if (entry.name == name) {
      return entry.format == static_cast<uint8_t>(format) && entry.arraySize == arraySize;
    }
  }
  return false;
}

}  // namespace

// ---- PrependDefines -------------------------------------------------------------------------

TEST(PrependDefinesTest, DefinesGoAfterTheVersionLine) {
  auto text = PrependDefines("#version 450\nvoid main() {}\n", {"HAS_UV", "COUNT=3"});
  EXPECT_EQ(text, "#version 450\n#define HAS_UV\n#define COUNT 3\nvoid main() {}\n");
}

TEST(PrependDefinesTest, AVersionLineAfterCommentsIsStillFound) {
  auto text = PrependDefines("// header\n// more\n#version 450\nvoid main() {}\n", {"A=1"});
  EXPECT_EQ(text, "// header\n// more\n#version 450\n#define A 1\nvoid main() {}\n");
}

TEST(PrependDefinesTest, WithoutAVersionLineTheDefinesComeFirst) {
  EXPECT_EQ(PrependDefines("void main() {}\n", {"A=1"}), "#define A 1\nvoid main() {}\n");
}

TEST(PrependDefinesTest, NoDefinesLeaveTheSourceUnchanged) {
  const std::string source = "#version 450\nvoid main() {}\n";
  EXPECT_EQ(PrependDefines(source, {}), source);
}

TEST(PrependDefinesTest, OnlyTheFirstEqualsSignSplitsNameFromValue) {
  EXPECT_EQ(PrependDefines("#version 450\n", {"EXPR=a==b"}), "#version 450\n#define EXPR a==b\n");
}

TEST(PrependDefinesTest, DefinesKeepTheirOrder) {
  auto text = PrependDefines("#version 450\n", {"C=3", "A=1", "B=2"});
  EXPECT_LT(text.find("#define C"), text.find("#define A"));
  EXPECT_LT(text.find("#define A"), text.find("#define B"));
}

// ---- PreprocessGLSL -------------------------------------------------------------------------

TEST(PreprocessGlslTest, ConditionalBlocksAreResolved) {
  const std::string source = std::string("#version 450\n#define FAST\n") +
                             "#ifdef FAST\nint chosen = 1;\n#else\nint chosen = 2;\n#endif\n"
                             "void main() {}\n";
  std::string error;
  auto text = PreprocessGLSL(source, ShaderStageType::Fragment, &error);
  ASSERT_FALSE(text.empty()) << error;
  EXPECT_NE(text.find("int chosen = 1;"), std::string::npos);
  EXPECT_EQ(text.find("int chosen = 2;"), std::string::npos);
  EXPECT_EQ(text.find("#ifdef"), std::string::npos);
}

TEST(PreprocessGlslTest, MacrosAreExpandedAndTheVersionLineIsKept) {
  std::string error;
  auto text = PreprocessGLSL("#version 450\n#define SCALE 4\nfloat v = SCALE;\nvoid main() {}\n",
                             ShaderStageType::Fragment, &error);
  ASSERT_FALSE(text.empty()) << error;
  EXPECT_NE(text.find("#version 450"), std::string::npos);
  EXPECT_NE(text.find("float v = 4;"), std::string::npos);
}

TEST(PreprocessGlslTest, LineDirectivesAreRemoved) {
  // A #line directive can sit between a layout qualifier and its declaration, which breaks the
  // line-oriented rewrites that consume this text. The preprocessor writes the ones the source
  // carries into its output, so a source with some shows whether they are stripped.
  const std::string source = std::string("#version 450\n") +
                             "layout(location = 0) out vec4 c;\n"
                             "#line 100\n"
                             "void main() {\n"
                             "#line 200\n"
                             "  c = vec4(1.0);\n"
                             "}\n";
  std::string error;
  auto text = PreprocessGLSL(source, ShaderStageType::Fragment, &error);
  ASSERT_FALSE(text.empty()) << error;
  EXPECT_EQ(text.find("#line"), std::string::npos) << text;
  // The code around the directives is untouched.
  EXPECT_NE(text.find("void main() {"), std::string::npos) << text;
  EXPECT_NE(text.find("c = vec4(1.0);"), std::string::npos) << text;
}

TEST(PreprocessGlslTest, APreprocessorErrorIsReported) {
  std::string error;
  auto text = PreprocessGLSL("#version 450\n#error stop here\nvoid main() {}\n",
                             ShaderStageType::Fragment, &error);
  EXPECT_TRUE(text.empty());
  EXPECT_NE(error.find("stop here"), std::string::npos) << error;
}

// ---- CompileGLSL ----------------------------------------------------------------------------

TEST(CompileGlslTest, AValidShaderProducesSpirv) {
  auto spirv = MustCompile(VertexSource, ShaderStageType::Vertex);
  ASSERT_FALSE(spirv.empty());
  EXPECT_EQ(spirv[0], 0x07230203u);  // The SPIR-V magic number.
}

TEST(CompileGlslTest, ASyntaxErrorFailsAndNamesTheVariant) {
  std::string error;
  auto spirv = CompileGLSL("#version 450\nvoid main() { this is not glsl }\n",
                           ShaderStageType::Fragment, "BrokenShader", 7, false, &error);
  EXPECT_TRUE(spirv.empty());
  EXPECT_NE(error.find("[BrokenShader variant 7]"), std::string::npos) << error;
}

TEST(CompileGlslTest, OptimizationShrinksTheModule) {
  const std::string source = std::string("#version 450\nlayout(location = 0) out vec4 c;\n") +
                             "float unused(float x) { return x * 2.0 + 1.0; }\n"
                             "void main() { float a = unused(1.0); c = vec4(0.5); }\n";
  std::string error;
  auto plain = CompileGLSL(source, ShaderStageType::Fragment, "S", 0, false, &error);
  ASSERT_FALSE(plain.empty()) << error;
  auto optimized = CompileGLSL(source, ShaderStageType::Fragment, "S", 0, true, &error);
  ASSERT_FALSE(optimized.empty()) << error;
  EXPECT_LT(optimized.size(), plain.size());
}

TEST(CompileGlslTest, TheSameSourceCompilesToTheSameModule) {
  auto first = MustCompile(FragmentSource, ShaderStageType::Fragment);
  auto second = MustCompile(FragmentSource, ShaderStageType::Fragment);
  EXPECT_EQ(first, second);
}

// ---- ExtractReflection ----------------------------------------------------------------------

TEST(ReflectionExtractorTest, TheVertexUniformBlockMembersAreExtracted) {
  auto vertex = MustCompile(VertexSource, ShaderStageType::Vertex);
  auto reflection = ExtractReflection(vertex, {});
  const auto& uniforms = reflection.vertexReflection.uniforms;
  ASSERT_EQ(uniforms.size(), 2u);
  EXPECT_TRUE(HasEntry(uniforms, "Matrix", UniformFormat::Float3x3));
  EXPECT_TRUE(HasEntry(uniforms, "Offset", UniformFormat::Float2));
}

TEST(ReflectionExtractorTest, VertexInputsComeInLocationOrderWithTheirGap) {
  auto vertex = MustCompile(VertexSource, ShaderStageType::Vertex);
  auto reflection = ExtractReflection(vertex, {});
  const auto& inputs = reflection.vertexReflection.vertexInputs;
  ASSERT_EQ(inputs.size(), 2u);
  EXPECT_EQ(inputs[0].name, "aPosition");
  EXPECT_EQ(inputs[0].location, 0);
  EXPECT_EQ(inputs[0].format, static_cast<uint8_t>(UniformFormat::Float2));
  EXPECT_EQ(inputs[1].name, "aCoverage");
  EXPECT_EQ(inputs[1].location, 2);
  EXPECT_EQ(inputs[1].format, static_cast<uint8_t>(UniformFormat::Float));
}

TEST(ReflectionExtractorTest, TheFragmentUniformBlockKeepsFormatsAndArraySizes) {
  auto fragment = MustCompile(FragmentSource, ShaderStageType::Fragment);
  auto reflection = ExtractReflection({}, fragment);
  const auto& uniforms = reflection.fragmentReflection.uniforms;
  ASSERT_EQ(uniforms.size(), 3u);
  EXPECT_TRUE(HasEntry(uniforms, "Color", UniformFormat::Float4));
  EXPECT_TRUE(HasEntry(uniforms, "Stops", UniformFormat::Float4, 4));
  EXPECT_TRUE(HasEntry(uniforms, "Mode", UniformFormat::Int));
}

TEST(ReflectionExtractorTest, SamplersAreRecordedInBindingOrderNotDeclarationOrder) {
  auto fragment = MustCompile(FragmentSource, ShaderStageType::Fragment);
  auto reflection = ExtractReflection({}, fragment);
  const auto& samplers = reflection.fragmentReflection.samplers;
  ASSERT_EQ(samplers.size(), 2u);
  // TextureSampler_1 is declared first but sits on binding 1.
  EXPECT_EQ(samplers[0].name, "TextureSampler_0");
  EXPECT_EQ(samplers[1].name, "TextureSampler_1");
  EXPECT_EQ(samplers[0].format, static_cast<uint8_t>(UniformFormat::Texture2DSampler));
}

TEST(ReflectionExtractorTest, ManySamplersDeclaredBackwardsStillComeInBindingOrder) {
  // The order spirv-cross lists resources in is not the binding order, and the runtime binds the
  // textures one after the other, so the extractor has to sort. Eight samplers declared from the
  // highest binding down leave no way for the sort to be a no-op.
  const int count = 8;
  std::string source = "#version 450\nlayout(location = 0) out vec4 c;\n";
  for (int binding = count - 1; binding >= 0; --binding) {
    source += "layout(set = 1, binding = " + std::to_string(binding) +
              ") uniform sampler2D TextureSampler_" + std::to_string(binding) + ";\n";
  }
  source += "void main() {\n  c = vec4(0.0);\n";
  for (int binding = count - 1; binding >= 0; --binding) {
    source += "  c += texture(TextureSampler_" + std::to_string(binding) + ", vec2(0.5));\n";
  }
  source += "}\n";
  auto fragment = MustCompile(source, ShaderStageType::Fragment);
  auto reflection = ExtractReflection({}, fragment);
  const auto& samplers = reflection.fragmentReflection.samplers;
  ASSERT_EQ(samplers.size(), static_cast<size_t>(count));
  for (int i = 0; i < count; ++i) {
    EXPECT_EQ(samplers[static_cast<size_t>(i)].name, "TextureSampler_" + std::to_string(i)) << i;
  }
}

TEST(ReflectionExtractorTest, ManyVertexInputsDeclaredBackwardsStillComeInLocationOrder) {
  const int count = 8;
  std::string source = "#version 450\n";
  for (int location = count - 1; location >= 0; --location) {
    source += "layout(location = " + std::to_string(location) + ") in float a" +
              std::to_string(location) + ";\n";
  }
  source += "void main() {\n  float sum = 0.0;\n";
  for (int location = count - 1; location >= 0; --location) {
    source += "  sum += a" + std::to_string(location) + ";\n";
  }
  source += "  gl_Position = vec4(sum);\n}\n";
  auto vertex = MustCompile(source, ShaderStageType::Vertex);
  auto reflection = ExtractReflection(vertex, {});
  const auto& inputs = reflection.vertexReflection.vertexInputs;
  ASSERT_EQ(inputs.size(), static_cast<size_t>(count));
  for (int i = 0; i < count; ++i) {
    EXPECT_EQ(inputs[static_cast<size_t>(i)].location, i) << i;
    EXPECT_EQ(inputs[static_cast<size_t>(i)].name, "a" + std::to_string(i)) << i;
  }
}

TEST(ReflectionExtractorTest, OnlyTheNamedBlockIsReflected) {
  // A block with another name is not the stage's uniform block, so its members are not reported.
  const std::string source = std::string("#version 450\n") +
                             "layout(std140, set = 0, binding = 0) uniform OtherBlock { vec4 X; };\n"
                             "void main() { gl_Position = X; }\n";
  auto vertex = MustCompile(source, ShaderStageType::Vertex);
  auto reflection = ExtractReflection(vertex, {});
  EXPECT_TRUE(reflection.vertexReflection.uniforms.empty());
}

TEST(ReflectionExtractorTest, EmptyStagesProduceEmptyReflection) {
  auto reflection = ExtractReflection({}, {});
  EXPECT_TRUE(reflection.vertexReflection.uniforms.empty());
  EXPECT_TRUE(reflection.vertexReflection.vertexInputs.empty());
  EXPECT_TRUE(reflection.fragmentReflection.uniforms.empty());
  EXPECT_TRUE(reflection.fragmentReflection.samplers.empty());
}

TEST(ReflectionExtractorTest, TheBlockNamesAreTheSharedContractNames) {
  // The extractor looks the blocks up by the names the runtime declares them with.
  EXPECT_STREQ(VertexUniformBlockName, "VertexUniformBlock");
  EXPECT_STREQ(FragmentUniformBlockName, "FragmentUniformBlock");
}

// ---- AnalyzeInlineExpansion / ValidateInlineExpansion ---------------------------------------

namespace {

// A fragment shader whose helper is called from `calls` sites. glslang keeps the function names,
// which is what the analysis reads.
std::string FragmentCalling(int calls, int bodyOperations) {
  std::string body;
  for (int i = 0; i < bodyOperations; ++i) {
    body += "  x = x * 1.0001 + " + std::to_string(i + 1) + ".0;\n";
  }
  std::string source = "#version 450\nlayout(location = 0) out vec4 c;\nlayout(location = 0) in float "
                       "v;\nfloat heavy(float x) {\n" +
                       body + "  return x;\n}\nvoid main() {\n  float r = v;\n";
  for (int i = 0; i < calls; ++i) {
    source += "  r = heavy(r + " + std::to_string(i) + ".0);\n";
  }
  source += "  c = vec4(r);\n}\n";
  return source;
}

InlineExpansion Analyze(const std::string& source) {
  auto spirv = MustCompile(source, ShaderStageType::Fragment);
  return AnalyzeInlineExpansion(spirv);
}

const InlinedFunction* FindFunction(const InlineExpansion& expansion, const std::string& name) {
  for (const auto& function : expansion.functions) {
    if (function.name == name) {
      return &function;
    }
  }
  return nullptr;
}

}  // namespace

TEST(InlineExpansionTest, ASingleCallSiteCopiesTheBodyOnce) {
  auto expansion = Analyze(FragmentCalling(1, 6));
  ASSERT_TRUE(expansion.valid);
  auto heavy = FindFunction(expansion, "heavy");
  ASSERT_NE(heavy, nullptr);
  EXPECT_EQ(heavy->copies, 1u);
}

TEST(InlineExpansionTest, EveryCallSiteAddsACopy) {
  auto expansion = Analyze(FragmentCalling(5, 6));
  ASSERT_TRUE(expansion.valid);
  auto heavy = FindFunction(expansion, "heavy");
  ASSERT_NE(heavy, nullptr);
  EXPECT_EQ(heavy->copies, 5u);
  EXPECT_GT(expansion.expandedInstructions, expansion.ownInstructions);
}

TEST(InlineExpansionTest, CopiesMultiplyThroughNestedCalls) {
  // inner is called twice from middle, middle twice from main: four copies of inner.
  const std::string source = std::string("#version 450\n") +
                             "layout(location = 0) out vec4 c;\nlayout(location = 0) in float v;\n"
                             "float inner(float x) { return x * 3.0 + 1.0; }\n"
                             "float middle(float x) { return inner(x) + inner(x + 1.0); }\n"
                             "void main() { c = vec4(middle(v) + middle(v + 2.0)); }\n";
  auto expansion = Analyze(source);
  ASSERT_TRUE(expansion.valid);
  auto inner = FindFunction(expansion, "inner");
  auto middle = FindFunction(expansion, "middle");
  ASSERT_NE(inner, nullptr);
  ASSERT_NE(middle, nullptr);
  EXPECT_EQ(middle->copies, 2u);
  EXPECT_EQ(inner->copies, 4u);
}

TEST(InlineExpansionTest, MalformedModulesAreInvalid) {
  EXPECT_FALSE(AnalyzeInlineExpansion({}).valid);
  EXPECT_FALSE(AnalyzeInlineExpansion({0x07230203u, 0, 0, 0}).valid);        // Header too short.
  EXPECT_FALSE(AnalyzeInlineExpansion({0xdeadbeefu, 0, 0, 0, 0}).valid);     // Wrong magic.
  // A well formed header followed by an instruction that claims more words than exist.
  EXPECT_FALSE(AnalyzeInlineExpansion({0x07230203u, 0, 0, 0, 0, (9u << 16) | 54u}).valid);
}

TEST(InlineExpansionTest, AModuleWithoutAnEntryPointIsInvalid) {
  EXPECT_FALSE(AnalyzeInlineExpansion({0x07230203u, 0, 0, 0, 0}).valid);
}

TEST(InlineExpansionPolicyTest, AFunctionDuplicatedWithinTheLimitPasses) {
  // A small helper called twice duplicates few instructions.
  InlineExpansionRecord record = {"S", 0, Analyze(FragmentCalling(2, 2))};
  ASSERT_TRUE(record.expansion.valid);
  EXPECT_EQ(ValidateInlineExpansion({record}), 0u);
}

TEST(InlineExpansionPolicyTest, AHeavyFunctionWithManyCallSitesIsAViolation) {
  InlineExpansionRecord record = {"HeavyShader", 3, Analyze(FragmentCalling(12, 60))};
  ASSERT_TRUE(record.expansion.valid);
  ::testing::internal::CaptureStderr();
  ::testing::internal::CaptureStdout();
  auto errors = ValidateInlineExpansion({record});
  auto out = ::testing::internal::GetCapturedStdout();
  auto err = ::testing::internal::GetCapturedStderr();
  EXPECT_GE(errors, 1u);
  EXPECT_TRUE(out.find("heavy") != std::string::npos || err.find("heavy") != std::string::npos)
      << out << err;
}

TEST(InlineExpansionPolicyTest, AHeavyFunctionWithOneCallSiteIsFine) {
  InlineExpansionRecord record = {"S", 0, Analyze(FragmentCalling(1, 60))};
  ASSERT_TRUE(record.expansion.valid);
  EXPECT_EQ(ValidateInlineExpansion({record}), 0u);
}

TEST(InlineExpansionPolicyTest, AnUnanalyzableModuleIsAViolationNotASilentPass) {
  // A module the analysis cannot read leaves the policy without coverage, so it fails the build
  // instead of letting a heavy function through unchecked.
  InlineExpansionRecord record;
  record.shaderName = "UnreadableShader";
  record.fragPermutationIndex = 4;
  EXPECT_FALSE(record.expansion.valid);
  ::testing::internal::CaptureStderr();
  auto errors = ValidateInlineExpansion({record});
  auto log = ::testing::internal::GetCapturedStderr();
  EXPECT_EQ(errors, 1u);
  EXPECT_NE(log.find("UnreadableShader (frag 4)"), std::string::npos) << log;
  EXPECT_NE(log.find("could not be analyzed"), std::string::npos) << log;
}

TEST(InlineExpansionPolicyTest, ViolationsOfOneFunctionAcrossVariantsAreReportedOnce) {
  // The same heavy function in several variants of one shader is one violation, not several.
  auto expansion = Analyze(FragmentCalling(12, 60));
  ASSERT_TRUE(expansion.valid);
  std::vector<InlineExpansionRecord> records;
  for (uint32_t variant = 0; variant < 4; ++variant) {
    records.push_back({"HeavyShader", variant, expansion});
  }
  ::testing::internal::CaptureStderr();
  auto errors = ValidateInlineExpansion(records);
  auto log = ::testing::internal::GetCapturedStderr();
  EXPECT_EQ(errors, 1u) << log;
  EXPECT_NE(log.find("4 variant(s)"), std::string::npos) << log;
}

TEST(InlineExpansionPolicyTest, ViolationsOfDifferentShadersAreCountedSeparately) {
  auto expansion = Analyze(FragmentCalling(12, 60));
  ASSERT_TRUE(expansion.valid);
  ::testing::internal::CaptureStderr();
  auto errors = ValidateInlineExpansion({{"ShaderA", 0, expansion}, {"ShaderB", 0, expansion}});
  ::testing::internal::GetCapturedStderr();
  EXPECT_EQ(errors, 2u);
}

TEST(InlineExpansionReportTest, TheReportListsEveryVariantAndItsDuplicatedFunctions) {
  auto expansion = Analyze(FragmentCalling(5, 6));
  ASSERT_TRUE(expansion.valid);
  auto path = ::testing::TempDir() + "tgfx_shader_tool_inline_report.json";
  ASSERT_TRUE(WriteInlineExpansionReport(path, {{"ShaderA", 2, expansion}}));
  std::ifstream file(path);
  std::stringstream buffer;
  buffer << file.rdbuf();
  auto text = buffer.str();
  EXPECT_NE(text.find("\"shader\": \"ShaderA\""), std::string::npos) << text;
  EXPECT_NE(text.find("\"frag\": 2"), std::string::npos) << text;
  EXPECT_NE(text.find("\"valid\": true"), std::string::npos) << text;
  EXPECT_NE(text.find("\"name\": \"heavy\""), std::string::npos) << text;
  EXPECT_NE(text.find("\"copies\": 5"), std::string::npos) << text;
  std::remove(path.c_str());
}

TEST(InlineExpansionReportTest, AnUnwritablePathFailsInsteadOfPretending) {
  EXPECT_FALSE(WriteInlineExpansionReport("/nonexistent/directory/report.json", {}));
}

}  // namespace tgfx

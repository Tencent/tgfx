/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License") you may not use this file except
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

#include <cstdlib>
#include <fstream>
#include "gpu/ir/ProgramIRBuilder.h"
#include "gpu/spirv/SPIRVProgramBuilder.h"
#include "gpu/spirv/SPIRVShaderBuilder.h"
#include "gtest/gtest.h"
#if defined(TGFX_USE_VULKAN)
#include <cstring>
#include <memory>
#include "tgfx/core/Surface.h"
#include "tgfx/gpu/GPU.h"
#include "tgfx/gpu/RenderPass.h"
#include "tgfx/gpu/RenderPipeline.h"
#include "tgfx/gpu/ShaderModule.h"
#include "tgfx/gpu/Texture.h"
#include "utils/ContextScope.h"
#endif

namespace tgfx {
namespace {

void WriteIfRequested(const std::vector<uint32_t>& words) {
  const auto* path = std::getenv("TGFX_SPIRV_OUT");
  if (path == nullptr || *path == '\0') {
    return;
  }
  std::ofstream out(path, std::ios::binary);
  out.write(reinterpret_cast<const char*>(words.data()),
            static_cast<std::streamsize>(words.size() * sizeof(uint32_t)));
}

bool ContainsOpcode(const std::vector<uint32_t>& words, uint16_t opcode) {
  for (size_t index = 5; index < words.size();) {
    const auto instruction = words[index];
    const auto wordCount = instruction >> 16;
    if (wordCount == 0) {
      return false;
    }
    if ((instruction & 0xFFFFu) == opcode) {
      return true;
    }
    index += wordCount;
  }
  return false;
}

}  // namespace

TEST(SPIRVShaderBuilderTest, EmitsConstantsConstructAndStore) {
  ProgramIRBuilder program = {};
  const auto zero = program.CreateConstant(0.0f);
  const auto color = program.CreateConstruct(SLType::Float4, {zero, zero, zero, zero});
  const auto output =
      program.CreateValue(SLType::Float4, Precision::High, IRValueKind::Local, "outputColor");
  program.StoreOutput(output, color);

  SPIRVShaderBuilder builder(ShaderStage::Fragment);
  std::string error = {};
  ASSERT_TRUE(builder.Begin(&error)) << error;
  uint32_t valueId = 0;
  ASSERT_TRUE(builder.EmitValue(program.IR(), color, &valueId, &error)) << error;
  EXPECT_NE(valueId, 0u);
  ASSERT_TRUE(builder.StoreOutput(program.IR(), color, &error)) << error;
  const auto words = builder.Finish(&error);
  ASSERT_FALSE(words.empty()) << error;
  WriteIfRequested(words);

  EXPECT_EQ(words[0], 0x07230203u);
  EXPECT_EQ(words[1], 0x00010000u);
  EXPECT_GT(words[3], 5u);
  EXPECT_TRUE(ContainsOpcode(words, spirv::kOpEntryPoint));
  EXPECT_TRUE(ContainsOpcode(words, spirv::kOpFunction));
  EXPECT_TRUE(ContainsOpcode(words, spirv::kOpFunctionEnd));
  EXPECT_TRUE(ContainsOpcode(words, spirv::kOpStore));
}

TEST(SPIRVShaderBuilderTest, EmitsSwizzleAndBinary) {
  ProgramIRBuilder program = {};
  const auto zero = program.CreateConstant(0.0f);
  const auto color = program.CreateConstruct(SLType::Float4, {zero, zero, zero, zero});
  const auto rgb = program.CreateSwizzle(color, SLType::Float3, "rgb");
  const auto doubled = program.CreateBinary(IRBinaryOp::Mul, SLType::Float3, rgb, rgb);
  const auto output = program.CreateConstruct(SLType::Float4, {doubled, zero});

  SPIRVShaderBuilder builder(ShaderStage::Fragment);
  std::string error = {};
  ASSERT_TRUE(builder.Begin(&error)) << error;
  uint32_t valueId = 0;
  ASSERT_TRUE(builder.EmitValue(program.IR(), output, &valueId, &error)) << error;
  const auto words = builder.Finish(&error);
  ASSERT_FALSE(words.empty()) << error;
  EXPECT_TRUE(ContainsOpcode(words, spirv::kOpVectorShuffle));
  EXPECT_TRUE(ContainsOpcode(words, spirv::kOpFMul));
  EXPECT_TRUE(ContainsOpcode(words, spirv::kOpCompositeConstruct));
}

TEST(SPIRVShaderBuilderTest, EmitsBuiltinFragCoord) {
  ProgramIRBuilder program = {};
  const auto fragCoord = program.CreateBuiltin(IRBuiltin::FragCoord, SLType::Float4);
  const auto xy = program.CreateSwizzle(fragCoord, SLType::Float2, "xy");
  const auto zero = program.CreateConstant(0.0f);
  const auto output = program.CreateConstruct(SLType::Float4, {xy, zero, zero});

  SPIRVShaderBuilder builder(ShaderStage::Fragment);
  std::string error = {};
  ASSERT_TRUE(builder.Begin(&error)) << error;
  uint32_t valueId = 0;
  ASSERT_TRUE(builder.EmitValue(program.IR(), output, &valueId, &error)) << error;
  ASSERT_TRUE(builder.StoreOutput(program.IR(), output, &error)) << error;
  const auto words = builder.Finish(&error);
  ASSERT_FALSE(words.empty()) << error;
  WriteIfRequested(words);

  EXPECT_TRUE(ContainsOpcode(words, spirv::kOpVariable));
  EXPECT_TRUE(ContainsOpcode(words, spirv::kOpLoad));
  EXPECT_TRUE(ContainsOpcode(words, spirv::kOpVectorShuffle));
}

TEST(SPIRVShaderBuilderTest, EmitsLocalLoadAndAssign) {
  ProgramIRBuilder program = {};
  const auto zero = program.CreateConstant(0.0f);
  const auto color = program.CreateConstruct(SLType::Float4, {zero, zero, zero, zero});
  const auto local =
      program.CreateValue(SLType::Float4, Precision::High, IRValueKind::Local, "color");
  const auto load = program.CreateLoad(local, SLType::Float4);
  const auto output =
      program.CreateValue(SLType::Float4, Precision::High, IRValueKind::Local, "outputColor");
  program.StoreOutput(output, load);

  SPIRVShaderBuilder builder(ShaderStage::Fragment);
  std::string error = {};
  ASSERT_TRUE(builder.Begin(&error)) << error;
  ASSERT_TRUE(builder.DeclareLocals(program.IR(), &error)) << error;
  ASSERT_TRUE(builder.Assign(program.IR(), {local, ""}, color, &error)) << error;
  ASSERT_TRUE(builder.StoreOutput(program.IR(), load, &error)) << error;
  const auto words = builder.Finish(&error);
  ASSERT_FALSE(words.empty()) << error;
  WriteIfRequested(words);

  EXPECT_TRUE(ContainsOpcode(words, spirv::kOpVariable));
  EXPECT_TRUE(ContainsOpcode(words, spirv::kOpStore));
  EXPECT_TRUE(ContainsOpcode(words, spirv::kOpLoad));
}

TEST(SPIRVShaderBuilderTest, EmitsTextureSample) {
  ProgramIRBuilder program = {};
  const auto sampler = program.AddSampler({"TextureSampler_0", SLType::Texture2DSampler, 0});
  const auto zero = program.CreateConstant(0.0f);
  const auto coord = program.CreateConstruct(SLType::Float2, {zero, zero});
  const auto sample = program.CreateTextureSample(sampler, coord, SLType::Float4, "textureColor",
                                                  SLType::Texture2DSampler);

  SPIRVShaderBuilder builder(ShaderStage::Fragment);
  std::string error = {};
  ASSERT_TRUE(builder.Begin(&error)) << error;
  uint32_t valueId = 0;
  ASSERT_TRUE(builder.EmitValue(program.IR(), sample, &valueId, &error)) << error;
  ASSERT_TRUE(builder.StoreOutput(program.IR(), sample, &error)) << error;
  const auto words = builder.Finish(&error);
  ASSERT_FALSE(words.empty()) << error;
  WriteIfRequested(words);

  EXPECT_TRUE(ContainsOpcode(words, spirv::kOpImageSampleImplicitLod));
  EXPECT_TRUE(ContainsOpcode(words, spirv::kOpVariable));
  EXPECT_TRUE(ContainsOpcode(words, spirv::kOpDecorate));
}

TEST(SPIRVShaderBuilderTest, EmitsUniformBlockLoad) {
  ProgramIRBuilder program = {};
  const auto uniform =
      program.AddUniform({"Color", SLType::Float4, Precision::High, ShaderStage::Fragment, 1});
  const auto load = program.CreateLoad(uniform, SLType::Float4);
  const auto output =
      program.CreateValue(SLType::Float4, Precision::High, IRValueKind::Local, "outputColor");
  program.StoreOutput(output, load);

  SPIRVShaderBuilder builder(ShaderStage::Fragment);
  std::string error = {};
  ASSERT_TRUE(builder.Begin(&error)) << error;
  uint32_t valueId = 0;
  ASSERT_TRUE(builder.EmitValue(program.IR(), load, &valueId, &error)) << error;
  ASSERT_TRUE(builder.StoreOutput(program.IR(), load, &error)) << error;
  const auto words = builder.Finish(&error);
  ASSERT_FALSE(words.empty()) << error;
  WriteIfRequested(words);

  EXPECT_TRUE(ContainsOpcode(words, spirv::kOpTypeStruct));
  EXPECT_TRUE(ContainsOpcode(words, spirv::kOpMemberDecorate));
  EXPECT_TRUE(ContainsOpcode(words, spirv::kOpAccessChain));
  EXPECT_TRUE(ContainsOpcode(words, spirv::kOpLoad));
}

TEST(SPIRVShaderBuilderTest, EmitsUniformArrayLoad) {
  ProgramIRBuilder program = {};
  const auto kernel = program.AddUniform(
      {"Kernel", SLType::Float4, Precision::High, ShaderStage::Fragment, 17});

  SPIRVShaderBuilder builder(ShaderStage::Fragment);
  std::string error = {};
  ASSERT_TRUE(builder.Begin(&error)) << error;
  uint32_t valueId = 0;
  ASSERT_TRUE(builder.EmitValue(program.IR(), kernel, &valueId, &error)) << error;
  const auto words = builder.Finish(&error);
  ASSERT_FALSE(words.empty()) << error;
  WriteIfRequested(words);

  EXPECT_TRUE(ContainsOpcode(words, spirv::kOpTypeArray));
  EXPECT_TRUE(ContainsOpcode(words, spirv::kOpAccessChain));
  EXPECT_TRUE(ContainsOpcode(words, spirv::kOpLoad));
}

TEST(SPIRVShaderBuilderTest, ImportsTheNamedSlotParameters) {
  const auto* desc = FindOperator(OperatorID::GaussianBlur1D);
  ASSERT_NE(desc, nullptr);
  OperatorArtifact artifact = {};
  artifact.id = OperatorID::GaussianBlur1D;
  artifact.backend = OperatorBackend::SPIRV;
  artifact.symbol = "gaussianBlur1D";

  SPIRVShaderBuilder builder(ShaderStage::Fragment);
  std::string error = {};
  ASSERT_TRUE(builder.Begin(&error)) << error;
  EXPECT_NE(builder.ImportFunction(*desc, artifact, &error), 0u) << error;
  const auto words = builder.Finish(&error);
  ASSERT_FALSE(words.empty()) << error;
  WriteIfRequested(words);

  // The kernel slots arrive as 17 named parameters: the imported module carries no array type.
  EXPECT_FALSE(ContainsOpcode(words, spirv::kOpTypeArray));
  EXPECT_TRUE(ContainsOpcode(words, spirv::kOpFunctionParameter));
}

TEST(SPIRVShaderBuilderTest, EmitsVertexPosition) {
  ProgramIRBuilder program = {};
  const auto zero = program.CreateConstant(0.0f);
  const auto one = program.CreateConstant(1.0f);
  const auto position = program.CreateConstruct(SLType::Float4, {zero, zero, zero, one});
  const auto output =
      program.CreateValue(SLType::Float4, Precision::High, IRValueKind::Local, "gl_Position");
  program.StoreOutput(output, position);

  SPIRVVertexShaderBuilder builder;
  std::string error = {};
  ASSERT_TRUE(builder.Begin(&error)) << error;
  uint32_t valueId = 0;
  ASSERT_TRUE(builder.EmitValue(program.IR(), position, &valueId, &error)) << error;
  ASSERT_TRUE(builder.StoreOutput(program.IR(), position, &error)) << error;
  const auto words = builder.Finish(&error);
  ASSERT_FALSE(words.empty()) << error;
  WriteIfRequested(words);

  EXPECT_TRUE(ContainsOpcode(words, spirv::kOpEntryPoint));
  EXPECT_TRUE(ContainsOpcode(words, spirv::kOpDecorate));
  EXPECT_TRUE(ContainsOpcode(words, spirv::kOpStore));
}

TEST(SPIRVShaderBuilderTest, RejectsUnsupportedExpressions) {
  ProgramIRBuilder program = {};
  const auto input =
      program.CreateValue(SLType::Float4, Precision::High, IRValueKind::Local, "inputColor");
  const auto result =
      program.CreateCall(OperatorID::Luma, SLType::Float4, {input, input, input, input});

  SPIRVShaderBuilder builder(ShaderStage::Fragment);
  std::string error = {};
  ASSERT_TRUE(builder.Begin(&error)) << error;
  uint32_t valueId = 0;
  EXPECT_FALSE(builder.EmitValue(program.IR(), result, &valueId, &error));
  EXPECT_FALSE(error.empty());
}

#if defined(TGFX_USE_VULKAN)
namespace {

std::vector<uint8_t> WordsToBytes(const std::vector<uint32_t>& words) {
  std::vector<uint8_t> bytes(words.size() * sizeof(uint32_t));
  std::memcpy(bytes.data(), words.data(), bytes.size());
  return bytes;
}

std::shared_ptr<ShaderModule> MakeAttributeVertexModule(GPU* gpu, std::string* error) {
  ProgramIRBuilder program = {};
  const auto positionAttr = program.AddAttribute({"aPosition", SLType::Float2, Precision::High, 0});
  const auto varying = program.AddVarying({"vColor", SLType::Float4, Precision::High, 0});
  const auto position = program.CreateLoad(positionAttr, SLType::Float2);
  const auto zero = program.CreateConstant(0.0f);
  const auto one = program.CreateConstant(1.0f);
  const auto clipPosition = program.CreateConstruct(SLType::Float4, {position, zero, one});
  const auto red = program.CreateConstruct(SLType::Float4, {one, zero, zero, one});
  const auto output =
      program.CreateValue(SLType::Float4, Precision::High, IRValueKind::Local, "gl_Position");
  program.StoreOutput(output, clipPosition);
  program.StoreVarying({varying, ""}, red);

  SPIRVVertexShaderBuilder builder;
  if (!builder.Begin(error)) {
    return nullptr;
  }
  uint32_t value = 0;
  if (!builder.EmitValue(program.IR(), clipPosition, &value, error) ||
      !builder.StoreOutput(program.IR(), clipPosition, error) ||
      !builder.StoreVarying(program.IR(), {varying, ""}, red, error)) {
    return nullptr;
  }
  const auto words = builder.Finish(error);
  if (words.empty()) {
    return nullptr;
  }
  ShaderModuleDescriptor descriptor = {};
  descriptor.format = ShaderCodeFormat::SPIRV;
  descriptor.stage = ShaderStage::Vertex;
  descriptor.binaryData = WordsToBytes(words);
  return gpu->createShaderModule(descriptor);
}

}  // namespace

TEST(SPIRVShaderBuilderTest, VulkanAcceptsGeneratedModules) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  if (context->backend() != Backend::Vulkan) {
    GTEST_SKIP() << "the Vulkan backend is not active";
  }
  auto* gpu = context->gpu();
  ASSERT_TRUE(gpu != nullptr);

  ProgramIRBuilder program = {};
  const auto zero = program.CreateConstant(0.0f);
  const auto one = program.CreateConstant(1.0f);
  const auto position = program.CreateConstruct(SLType::Float4, {zero, zero, zero, one});
  const auto positionOutput =
      program.CreateValue(SLType::Float4, Precision::High, IRValueKind::Local, "gl_Position");
  program.StoreOutput(positionOutput, position);

  SPIRVVertexShaderBuilder vertexBuilder;
  std::string error = {};
  ASSERT_TRUE(vertexBuilder.Begin(&error)) << error;
  uint32_t vertexValue = 0;
  ASSERT_TRUE(vertexBuilder.EmitValue(program.IR(), position, &vertexValue, &error)) << error;
  ASSERT_TRUE(vertexBuilder.StoreOutput(program.IR(), position, &error)) << error;
  const auto vertexWords = vertexBuilder.Finish(&error);
  ASSERT_FALSE(vertexWords.empty()) << error;

  ProgramIRBuilder fragmentProgram = {};
  const auto colorZero = fragmentProgram.CreateConstant(0.0f);
  const auto colorOne = fragmentProgram.CreateConstant(1.0f);
  const auto color =
      fragmentProgram.CreateConstruct(SLType::Float4, {colorZero, colorZero, colorZero, colorOne});
  const auto colorOutput = fragmentProgram.CreateValue(SLType::Float4, Precision::High,
                                                       IRValueKind::Local, "outputColor");
  fragmentProgram.StoreOutput(colorOutput, color);

  SPIRVFragmentShaderBuilder fragmentBuilder;
  ASSERT_TRUE(fragmentBuilder.Begin(&error)) << error;
  uint32_t fragmentValue = 0;
  ASSERT_TRUE(fragmentBuilder.EmitValue(fragmentProgram.IR(), color, &fragmentValue, &error))
      << error;
  ASSERT_TRUE(fragmentBuilder.StoreOutput(fragmentProgram.IR(), color, &error)) << error;
  const auto fragmentWords = fragmentBuilder.Finish(&error);
  ASSERT_FALSE(fragmentWords.empty()) << error;

  auto toBytes = [](const std::vector<uint32_t>& words) {
    std::vector<uint8_t> bytes(words.size() * sizeof(uint32_t));
    std::memcpy(bytes.data(), words.data(), bytes.size());
    return bytes;
  };
  ShaderModuleDescriptor vertexDesc = {};
  vertexDesc.format = ShaderCodeFormat::SPIRV;
  vertexDesc.stage = ShaderStage::Vertex;
  vertexDesc.binaryData = toBytes(vertexWords);
  auto vertexModule = gpu->createShaderModule(vertexDesc);
  ASSERT_TRUE(vertexModule != nullptr);

  ShaderModuleDescriptor fragmentDesc = {};
  fragmentDesc.format = ShaderCodeFormat::SPIRV;
  fragmentDesc.stage = ShaderStage::Fragment;
  fragmentDesc.binaryData = toBytes(fragmentWords);
  auto fragmentModule = gpu->createShaderModule(fragmentDesc);
  ASSERT_TRUE(fragmentModule != nullptr);

  RenderPipelineDescriptor pipelineDesc = {};
  pipelineDesc.vertex.module = vertexModule;
  pipelineDesc.fragment.module = fragmentModule;
  pipelineDesc.fragment.colorAttachments.push_back({});
  auto pipeline = gpu->createRenderPipeline(pipelineDesc);
  ASSERT_TRUE(pipeline != nullptr) << "the generated SPIR-V modules did not form a pipeline";
}

TEST(SPIRVShaderBuilderTest, VulkanAcceptsUniformAndSamplerPipeline) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  if (context->backend() != Backend::Vulkan) {
    GTEST_SKIP() << "the Vulkan backend is not active";
  }
  auto* gpu = context->gpu();
  ASSERT_TRUE(gpu != nullptr);

  std::string error = {};
  ProgramIRBuilder vertexProgram = {};
  const auto zero = vertexProgram.CreateConstant(0.0f);
  const auto one = vertexProgram.CreateConstant(1.0f);
  const auto position = vertexProgram.CreateConstruct(SLType::Float4, {zero, zero, zero, one});
  const auto positionOutput =
      vertexProgram.CreateValue(SLType::Float4, Precision::High, IRValueKind::Local, "gl_Position");
  vertexProgram.StoreOutput(positionOutput, position);

  ProgramIRBuilder fragmentProgram = {};
  const auto tint = fragmentProgram.AddUniform(
      {"Tint", SLType::Float4, Precision::High, ShaderStage::Fragment, 1});
  const auto sampler =
      fragmentProgram.AddSampler({"TextureSampler_0", SLType::Texture2DSampler, 0});
  const auto fragmentZero = fragmentProgram.CreateConstant(0.0f);
  const auto coord = fragmentProgram.CreateConstruct(SLType::Float2, {fragmentZero, fragmentZero});
  const auto sample = fragmentProgram.CreateTextureSample(sampler, coord, SLType::Float4,
                                                          "textureColor", SLType::Texture2DSampler);
  const auto color = fragmentProgram.CreateBinary(IRBinaryOp::Mul, SLType::Float4, sample, tint);
  const auto output = fragmentProgram.CreateValue(SLType::Float4, Precision::High,
                                                  IRValueKind::Local, "outputColor");
  fragmentProgram.StoreOutput(output, color);

  SPIRVProgramModules modules = {};
  ASSERT_TRUE(
      SPIRVProgramBuilder::Build(vertexProgram.IR(), fragmentProgram.IR(), &modules, &error))
      << error;

  ShaderModuleDescriptor vertexDesc = {};
  vertexDesc.format = ShaderCodeFormat::SPIRV;
  vertexDesc.stage = ShaderStage::Vertex;
  vertexDesc.binaryData = WordsToBytes(modules.vertex);
  auto vertexModule = gpu->createShaderModule(vertexDesc);
  ASSERT_TRUE(vertexModule != nullptr);

  ShaderModuleDescriptor fragmentDesc = {};
  fragmentDesc.format = ShaderCodeFormat::SPIRV;
  fragmentDesc.stage = ShaderStage::Fragment;
  fragmentDesc.binaryData = WordsToBytes(modules.fragment);
  fragmentDesc.uniformSlots = {{"FragmentUniforms", 0}};
  auto fragmentModule = gpu->createShaderModule(fragmentDesc);
  ASSERT_TRUE(fragmentModule != nullptr);

  BindingLayout bindingLayout = {};
  ASSERT_TRUE(modules.layout.BuildBindingLayout(&bindingLayout, &error)) << error;

  RenderPipelineDescriptor pipelineDesc = {};
  pipelineDesc.vertex.module = vertexModule;
  pipelineDesc.fragment.module = fragmentModule;
  pipelineDesc.fragment.colorAttachments.push_back({});
  pipelineDesc.layout = bindingLayout;
  auto pipeline = gpu->createRenderPipeline(pipelineDesc);
  ASSERT_TRUE(pipeline != nullptr) << "the generated layout did not match the SPIR-V";
}

TEST(SPIRVShaderBuilderTest, VulkanDrawsGeneratedTriangle) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  if (context->backend() != Backend::Vulkan) {
    GTEST_SKIP() << "the Vulkan backend is not active";
  }
  auto* gpu = context->gpu();
  ASSERT_TRUE(gpu != nullptr);

  std::string error = {};
  auto vertexModule = MakeAttributeVertexModule(gpu, &error);
  ASSERT_TRUE(vertexModule != nullptr) << error;

  ProgramIRBuilder fragmentProgram = {};
  const auto varying = fragmentProgram.AddVarying({"vColor", SLType::Float4, Precision::High, 0});
  const auto output = fragmentProgram.CreateValue(SLType::Float4, Precision::High,
                                                  IRValueKind::Local, "outputColor");
  fragmentProgram.StoreOutput(output, varying);

  SPIRVFragmentShaderBuilder fragmentBuilder;
  ASSERT_TRUE(fragmentBuilder.Begin(&error)) << error;
  uint32_t value = 0;
  ASSERT_TRUE(fragmentBuilder.EmitValue(fragmentProgram.IR(), varying, &value, &error)) << error;
  ASSERT_TRUE(fragmentBuilder.StoreOutput(fragmentProgram.IR(), varying, &error)) << error;
  const auto fragmentWords = fragmentBuilder.Finish(&error);
  ASSERT_FALSE(fragmentWords.empty()) << error;

  ShaderModuleDescriptor fragmentDesc = {};
  fragmentDesc.format = ShaderCodeFormat::SPIRV;
  fragmentDesc.stage = ShaderStage::Fragment;
  fragmentDesc.binaryData = WordsToBytes(fragmentWords);
  auto fragmentModule = gpu->createShaderModule(fragmentDesc);
  ASSERT_TRUE(fragmentModule != nullptr);

  RenderPipelineDescriptor pipelineDesc = {};
  Attribute positionAttribute = {"aPosition", VertexFormat::Float2};
  pipelineDesc.vertex.bufferLayouts = {VertexBufferLayout({positionAttribute})};
  pipelineDesc.vertex.module = vertexModule;
  pipelineDesc.fragment.module = fragmentModule;
  pipelineDesc.fragment.colorAttachments.push_back({});
  auto pipeline = gpu->createRenderPipeline(pipelineDesc);
  ASSERT_TRUE(pipeline != nullptr);

  struct Vertex {
    float x;
    float y;
  };
  const Vertex vertices[] = {{-1.0f, -1.0f}, {1.0f, -1.0f}, {-1.0f, 1.0f}, {1.0f, 1.0f}};
  auto vertexBuffer = gpu->createBuffer(sizeof(vertices), GPUBufferUsage::VERTEX);
  ASSERT_TRUE(vertexBuffer != nullptr);
  auto* mapped = static_cast<Vertex*>(vertexBuffer->map());
  ASSERT_TRUE(mapped != nullptr);
  std::memcpy(mapped, vertices, sizeof(vertices));
  vertexBuffer->unmap();

  constexpr int SIZE = 64;
  TextureDescriptor renderTextureDesc(
      SIZE, SIZE, PixelFormat::RGBA_8888, false, 1,
      TextureUsage::RENDER_ATTACHMENT | TextureUsage::TEXTURE_BINDING);
  auto renderTexture = gpu->createTexture(renderTextureDesc);
  ASSERT_TRUE(renderTexture != nullptr);

  auto encoder = gpu->createCommandEncoder();
  ASSERT_TRUE(encoder != nullptr);
  RenderPassDescriptor renderPassDesc(renderTexture, LoadAction::Clear, StoreAction::Store,
                                      PMColor::Transparent());
  auto renderPass = encoder->beginRenderPass(renderPassDesc);
  ASSERT_TRUE(renderPass != nullptr);
  renderPass->setPipeline(std::move(pipeline));
  renderPass->setVertexBuffer(0, vertexBuffer);
  renderPass->draw(PrimitiveType::TriangleStrip, 4, 1);
  renderPass->end();

  auto commandBuffer = encoder->finish();
  ASSERT_TRUE(commandBuffer != nullptr);
  gpu->queue()->submit(commandBuffer);
  gpu->queue()->waitUntilCompleted();

  auto surface =
      Surface::MakeFrom(context, renderTexture->getBackendTexture(), ImageOrigin::TopLeft);
  ASSERT_TRUE(surface != nullptr);
  auto info = ImageInfo::Make(1, 1, ColorType::RGBA_8888, AlphaType::Premultiplied);
  uint32_t centerPixel = 0;
  ASSERT_TRUE(surface->readPixels(info, &centerPixel, SIZE / 2, SIZE / 2));
  EXPECT_EQ(centerPixel, 0xFF0000FFu) << "the generated shaders did not pass the varying";
}
#endif

}  // namespace tgfx

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

#include <functional>
#include <set>
#include "gpu/GlobalCache.h"
#include "gpu/PrecompiledShaderCache.h"
#include "gpu/ShaderCache.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Shader.h"
#include "tgfx/core/Surface.h"
#include "utils/TestUtils.h"

namespace tgfx {

static ShaderModuleKey KeyOf(const ShaderModuleDescriptor& descriptor) {
  std::string encoded;
  EncodeShaderModuleDescriptor(descriptor, &encoded);
  return MakeShaderModuleKey(encoded);
}

// Every field a backend reads to build a module must change the key; otherwise two different
// shaders would share one module. A field added to ShaderModuleDescriptor needs a case here.
TGFX_TEST(ShaderCacheTest, ModuleKeyDistinguishesEveryField) {
  ShaderModuleDescriptor base = {};
  base.format = ShaderCodeFormat::GLSL;
  base.stage = ShaderStage::Fragment;
  base.code = "void main() {}";
  base.uniformSlots = {{"FragmentUniformBlock", 1}};
  auto baseKey = KeyOf(base);
  EXPECT_TRUE(KeyOf(base) == baseKey);

  std::vector<std::pair<const char*, std::function<void(ShaderModuleDescriptor*)>>> mutations = {
      {"format", [](ShaderModuleDescriptor* d) { d->format = ShaderCodeFormat::SPIRV; }},
      {"stage", [](ShaderModuleDescriptor* d) { d->stage = ShaderStage::Vertex; }},
      {"code", [](ShaderModuleDescriptor* d) { d->code += " "; }},
      {"binaryData", [](ShaderModuleDescriptor* d) { d->binaryData = {0}; }},
      {"uniform slot value",
       [](ShaderModuleDescriptor* d) { d->uniformSlots.begin()->second = 2; }},
      {"uniform slot name",
       [](ShaderModuleDescriptor* d) {
         d->uniformSlots.clear();
         d->uniformSlots["VertexUniformBlock"] = 1;
       }},
      {"extra uniform slot", [](ShaderModuleDescriptor* d) { d->uniformSlots["Extra"] = 0; }},
      {"no uniform slots", [](ShaderModuleDescriptor* d) { d->uniformSlots.clear(); }},
  };
  for (const auto& [name, mutate] : mutations) {
    auto changed = base;
    mutate(&changed);
    EXPECT_FALSE(KeyOf(changed) == baseKey) << "the key ignores " << name;
  }

  // Moving bytes from one field to the next must not produce the same encoding.
  ShaderModuleDescriptor left = base;
  left.code = "ab";
  ShaderModuleDescriptor right = base;
  right.code = "a";
  right.binaryData = {'b'};
  EXPECT_FALSE(KeyOf(left) == KeyOf(right));
}

static void DrawGradientRect(Context* context, Surface* surface) {
  Paint paint = {};
  paint.setShader(Shader::MakeLinearGradient({0, 0}, {64, 64}, {Color::Red(), Color::Blue()}));
  surface->getCanvas()->drawRect(Rect::MakeWH(64, 64), paint);
  context->flushAndSubmit(true);
}

// A program created again after the program cache dropped it reuses the cached modules instead of
// compiling them again, on whichever route (precompiled or runtime) the draw takes.
TGFX_TEST(ShaderCacheTest, RecreatedProgramReusesModules) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 64, 64);
  ASSERT_TRUE(surface != nullptr);
  auto* globalCache = context->globalCache();
  auto* shaderCache = context->precompiledShaderCache()->shaderCache();
  if (!shaderCache->moduleCacheActive()) {
    GTEST_SKIP() << "the module cache is disabled (TGFX_SHADER_CACHE_DISABLE)";
  }

  globalCache->clearPrograms();
  shaderCache->clear();
  DrawGradientRect(context, surface.get());
  auto first = shaderCache->stats();
  ASSERT_GT(first.moduleCreations, 0u);

  globalCache->clearPrograms();
  DrawGradientRect(context, surface.get());
  auto second = shaderCache->stats();
  EXPECT_GT(second.moduleRequests, first.moduleRequests);
  EXPECT_EQ(second.moduleCreations, first.moduleCreations);
  EXPECT_EQ(second.moduleHits - first.moduleHits, second.moduleRequests - first.moduleRequests);

  // Clearing the shader cache really drops the modules: the same draw compiles them again.
  globalCache->clearPrograms();
  shaderCache->clear();
  EXPECT_EQ(shaderCache->moduleCount(), 0u);
  DrawGradientRect(context, surface.get());
  EXPECT_GT(shaderCache->stats().moduleCreations, second.moduleCreations);
}

// A descriptor the GPU rejects is not cached: every request tries again and reports the failure.
// Only checked where module creation compiles the source and reports a syntax error; a backend
// that defers compilation to pipeline creation returns a module for any source.
TGFX_TEST(ShaderCacheTest, FailedModuleIsNotCached) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto* shaderCache = context->precompiledShaderCache()->shaderCache();
  ShaderModuleDescriptor broken = {};
  broken.stage = ShaderStage::Fragment;
  broken.code = "this is not a shader";
  if (context->gpu()->createShaderModule(broken) != nullptr) {
    GTEST_SKIP() << "this backend does not reject an invalid shader at module creation";
  }
  auto before = shaderCache->stats();
  auto countBefore = shaderCache->moduleCount();
  EXPECT_TRUE(shaderCache->findOrCreateModule(broken) == nullptr);
  EXPECT_TRUE(shaderCache->findOrCreateModule(broken) == nullptr);
  auto after = shaderCache->stats();
  EXPECT_EQ(after.moduleCreationFailures - before.moduleCreationFailures, 2u);
  EXPECT_EQ(after.moduleHits, before.moduleHits);
  EXPECT_EQ(shaderCache->moduleCount(), countBefore);
}

// A stand-in module: pipeline encoding only needs a module's identity, mapped to a content key.
class FakeModule : public ShaderModule {};

static RenderPipelineDescriptor MakeFullDescriptor(const std::shared_ptr<ShaderModule>& vertex,
                                                   const std::shared_ptr<ShaderModule>& fragment) {
  RenderPipelineDescriptor descriptor = {};
  descriptor.vertex.module = vertex;
  descriptor.vertex.bufferLayouts = {
      VertexBufferLayout({{"position", VertexFormat::Float2}, {"color", VertexFormat::Float4}})};
  descriptor.fragment.module = fragment;
  descriptor.fragment.colorAttachments.push_back({});
  descriptor.layout.uniformBlocks = {{"VertexUniformBlock", 0, ShaderVisibility::Vertex}};
  descriptor.layout.textureSamplers = {{"TextureSampler_0", 0, ShaderVisibility::Fragment}};
  return descriptor;
}

// Every field of RenderPipelineDescriptor must change the pipeline key; otherwise two programs
// would share a pipeline with the wrong state. A field added to the descriptor (or to a type it
// contains) needs a case here.
TGFX_TEST(ShaderCacheTest, PipelineKeyDistinguishesEveryField) {
  auto vertexA = std::make_shared<FakeModule>();
  auto fragmentA = std::make_shared<FakeModule>();
  auto fragmentB = std::make_shared<FakeModule>();
  auto twinOfFragmentA = std::make_shared<FakeModule>();
  ShaderModuleKey keyVertexA = {};
  keyVertexA.digest[0] = 1;
  ShaderModuleKey keyFragmentA = {};
  keyFragmentA.digest[0] = 2;
  ShaderModuleKey keyFragmentB = {};
  keyFragmentB.digest[0] = 3;
  auto keyOf = [&](const ShaderModule* module) -> const ShaderModuleKey* {
    if (module == vertexA.get()) {
      return &keyVertexA;
    }
    if (module == fragmentA.get() || module == twinOfFragmentA.get()) {
      return &keyFragmentA;
    }
    if (module == fragmentB.get()) {
      return &keyFragmentB;
    }
    return nullptr;
  };
  auto encode = [&](const RenderPipelineDescriptor& descriptor) {
    std::string encoded;
    EXPECT_TRUE(EncodeRenderPipelineDescriptor(descriptor, keyOf, &encoded));
    return encoded;
  };
  auto base = MakeFullDescriptor(vertexA, fragmentA);
  auto baseEncoded = encode(base);

  // Different module objects with the same content key are the same pipeline.
  EXPECT_EQ(encode(MakeFullDescriptor(vertexA, twinOfFragmentA)), baseEncoded);

  using Mutation = std::function<void(RenderPipelineDescriptor*)>;
  std::vector<std::pair<const char*, Mutation>> mutations = {
      {"vertex module", [&](auto* d) { d->vertex.module = fragmentB; }},
      {"vertex entry point", [](auto* d) { d->vertex.entryPoint = "main2"; }},
      {"bind attributes by location", [](auto* d) { d->vertex.bindAttributesByLocation = true; }},
      {"buffer layout count", [](auto* d) { d->vertex.bufferLayouts.push_back({}); }},
      {"buffer stride", [](auto* d) { d->vertex.bufferLayouts[0].stride += 4; }},
      {"buffer step mode",
       [](auto* d) { d->vertex.bufferLayouts[0].stepMode = VertexStepMode::Instance; }},
      {"attribute count", [](auto* d) { d->vertex.bufferLayouts[0].attributes.pop_back(); }},
      {"attribute name",
       [](auto* d) {
         d->vertex.bufferLayouts[0].attributes[0] = {"position2", VertexFormat::Float2};
       }},
      {"attribute format",
       [](auto* d) {
         d->vertex.bufferLayouts[0].attributes[0] = {"position", VertexFormat::Float4};
       }},
      {"fragment module", [&](auto* d) { d->fragment.module = fragmentB; }},
      {"fragment entry point", [](auto* d) { d->fragment.entryPoint = "main2"; }},
      {"color attachment count", [](auto* d) { d->fragment.colorAttachments.push_back({}); }},
      {"color format",
       [](auto* d) { d->fragment.colorAttachments[0].format = PixelFormat::BGRA_8888; }},
      {"blend enable", [](auto* d) { d->fragment.colorAttachments[0].blendEnable = true; }},
      {"src color factor",
       [](auto* d) { d->fragment.colorAttachments[0].srcColorBlendFactor = BlendFactor::Src; }},
      {"dst color factor",
       [](auto* d) { d->fragment.colorAttachments[0].dstColorBlendFactor = BlendFactor::Src; }},
      {"color blend op",
       [](auto* d) { d->fragment.colorAttachments[0].colorBlendOp = BlendOperation::Subtract; }},
      {"src alpha factor",
       [](auto* d) { d->fragment.colorAttachments[0].srcAlphaBlendFactor = BlendFactor::Src; }},
      {"dst alpha factor",
       [](auto* d) { d->fragment.colorAttachments[0].dstAlphaBlendFactor = BlendFactor::Src; }},
      {"alpha blend op",
       [](auto* d) { d->fragment.colorAttachments[0].alphaBlendOp = BlendOperation::Subtract; }},
      {"color write mask", [](auto* d) { d->fragment.colorAttachments[0].colorWriteMask = 0; }},
      {"uniform block count", [](auto* d) { d->layout.uniformBlocks.clear(); }},
      {"uniform block name", [](auto* d) { d->layout.uniformBlocks[0].name = "Other"; }},
      {"uniform block binding", [](auto* d) { d->layout.uniformBlocks[0].binding = 1; }},
      {"uniform block visibility",
       [](auto* d) { d->layout.uniformBlocks[0].visibility = ShaderVisibility::Fragment; }},
      {"sampler count", [](auto* d) { d->layout.textureSamplers.clear(); }},
      {"sampler name", [](auto* d) { d->layout.textureSamplers[0].name = "Other"; }},
      {"sampler binding", [](auto* d) { d->layout.textureSamplers[0].binding = 1; }},
      {"sampler visibility",
       [](auto* d) { d->layout.textureSamplers[0].visibility = ShaderVisibility::Vertex; }},
      {"depth compare", [](auto* d) { d->depthStencil.depthCompare = CompareFunction::Less; }},
      {"depth write", [](auto* d) { d->depthStencil.depthWriteEnabled = true; }},
      {"stencil back compare",
       [](auto* d) { d->depthStencil.stencilBack.compare = CompareFunction::Equal; }},
      {"stencil back depth fail",
       [](auto* d) { d->depthStencil.stencilBack.depthFailOp = StencilOperation::Zero; }},
      {"stencil back fail",
       [](auto* d) { d->depthStencil.stencilBack.failOp = StencilOperation::Zero; }},
      {"stencil back pass",
       [](auto* d) { d->depthStencil.stencilBack.passOp = StencilOperation::Zero; }},
      {"stencil front compare",
       [](auto* d) { d->depthStencil.stencilFront.compare = CompareFunction::Equal; }},
      {"stencil front depth fail",
       [](auto* d) { d->depthStencil.stencilFront.depthFailOp = StencilOperation::Zero; }},
      {"stencil front fail",
       [](auto* d) { d->depthStencil.stencilFront.failOp = StencilOperation::Zero; }},
      {"stencil front pass",
       [](auto* d) { d->depthStencil.stencilFront.passOp = StencilOperation::Zero; }},
      {"stencil read mask", [](auto* d) { d->depthStencil.stencilReadMask = 0xF; }},
      {"stencil write mask", [](auto* d) { d->depthStencil.stencilWriteMask = 0xF; }},
      {"depth stencil format",
       [](auto* d) { d->depthStencil.format = PixelFormat::DEPTH24_STENCIL8; }},
      {"cull mode", [](auto* d) { d->primitive.cullMode = CullMode::Back; }},
      {"front face", [](auto* d) { d->primitive.frontFace = FrontFace::CW; }},
      {"sample count", [](auto* d) { d->multisample.count = 4; }},
      {"sample mask", [](auto* d) { d->multisample.mask = 0x1; }},
      {"alpha to coverage", [](auto* d) { d->multisample.alphaToCoverageEnabled = true; }},
  };
  std::set<std::string> seen = {baseEncoded};
  for (const auto& [name, mutate] : mutations) {
    auto changed = base;
    mutate(&changed);
    auto encoded = encode(changed);
    EXPECT_NE(encoded, baseEncoded) << "the pipeline key ignores " << name;
    EXPECT_TRUE(seen.insert(encoded).second) << name << " encodes like another mutation";
  }

  // A module the cache does not know cannot be keyed: the descriptor is not cacheable.
  std::string encoded;
  EXPECT_FALSE(EncodeRenderPipelineDescriptor(
      MakeFullDescriptor(vertexA, std::make_shared<FakeModule>()), keyOf, &encoded));
}

// A program created again after the program cache dropped it reuses its cached pipeline, and the
// reuse is not counted as a runtime pipeline creation.
TGFX_TEST(ShaderCacheTest, RecreatedProgramReusesPipeline) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  auto surface = Surface::Make(context, 64, 64);
  ASSERT_TRUE(surface != nullptr);
  auto* globalCache = context->globalCache();
  auto* shaderCache = context->precompiledShaderCache()->shaderCache();
  if (!shaderCache->pipelineCacheActive()) {
    GTEST_SKIP() << "the pipeline cache is disabled (TGFX_SHADER_CACHE_DISABLE)";
  }

  globalCache->clearPrograms();
  shaderCache->clear();
  DrawGradientRect(context, surface.get());
  auto first = shaderCache->stats();
  auto firstPrograms = globalCache->programStats();
  ASSERT_GT(first.pipelineCreations, 0u);

  globalCache->clearPrograms();
  DrawGradientRect(context, surface.get());
  auto second = shaderCache->stats();
  auto secondPrograms = globalCache->programStats();
  EXPECT_GT(second.pipelineRequests, first.pipelineRequests);
  EXPECT_EQ(second.pipelineCreations, first.pipelineCreations);
  EXPECT_EQ(second.pipelineHits - first.pipelineHits,
            second.pipelineRequests - first.pipelineRequests);
  EXPECT_EQ(secondPrograms.runtimePipelineCreationAttempts,
            firstPrograms.runtimePipelineCreationAttempts);
  EXPECT_GT(secondPrograms.cacheMisses, firstPrograms.cacheMisses);
}

// Different programs whose descriptors are identical share one pipeline. The precompiled route is
// where this happens: its generic shaders serve many processor combinations, and the blend modes
// of the advanced-blend rects below differ only in a uniform.
TGFX_TEST(ShaderCacheTest, DistinctProgramsShareIdenticalPipeline) {
  ContextScope scope;
  auto context = scope.getContext();
  ASSERT_TRUE(context != nullptr);
  if (!context->precompiledShaderCache()->isLoaded()) {
    GTEST_SKIP() << "no precompiled bundle is loaded (runtime route): nothing to share here";
  }
  auto surface = Surface::Make(context, 256, 64);
  ASSERT_TRUE(surface != nullptr);
  auto* globalCache = context->globalCache();
  auto* shaderCache = context->precompiledShaderCache()->shaderCache();
  if (!shaderCache->pipelineCacheActive()) {
    GTEST_SKIP() << "the pipeline cache is disabled (TGFX_SHADER_CACHE_DISABLE)";
  }
  globalCache->clearPrograms();
  shaderCache->clear();
  auto before = shaderCache->stats();
  auto programsBefore = globalCache->programStats();
  const BlendMode modes[] = {BlendMode::Multiply, BlendMode::Screen, BlendMode::Overlay,
                             BlendMode::Darken};
  float x = 0;
  for (auto mode : modes) {
    Paint paint = {};
    paint.setColor(Color::Blue());
    paint.setBlendMode(mode);
    surface->getCanvas()->drawRect(Rect::MakeXYWH(x, 0.f, 60.f, 60.f), paint);
    x += 64;
  }
  context->flushAndSubmit(true);
  auto after = shaderCache->stats();
  auto programsAfter = globalCache->programStats();
  auto newPrograms = programsAfter.cacheMisses - programsBefore.cacheMisses;
  auto pipelineHits = after.pipelineHits - before.pipelineHits;
  EXPECT_GT(newPrograms, 1u);
  EXPECT_GT(pipelineHits, 0u) << newPrograms << " programs, no shared pipeline";
}

}  // namespace tgfx

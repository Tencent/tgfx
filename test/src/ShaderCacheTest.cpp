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
#include "gpu/GlobalCache.h"
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
  auto* shaderCache = globalCache->shaderCache();

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
  auto* shaderCache = context->globalCache()->shaderCache();
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

}  // namespace tgfx

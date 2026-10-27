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

// The Vulkan emitter is the one emitter with a fail path of its own: an optimization compile
// that fails must fail the variant (a silent fallback would store a bigger module nobody
// asked for). These tests pin that with a real StageCompiler and a source that cannot compile,
// and pin the success path with one that can.

#include <memory>
#include <string>
#include <vector>
#include "compile/StageCompiler.h"
#include "core/DiagnosticCodes.h"
#include "emit/ProfileEmitter.h"
#include "gpu/shaders/AllShaderFamilies.h"
#include "gpu/shaders/PrecompiledShader.h"
#include "gtest/gtest.h"

namespace tgfx {
namespace {

// A syntactically valid vertex/fragment pair too small to mean anything, but compilable.
// (Plain escaped strings, not raw strings: this file has CRLF line endings, and a raw string
// would feed the carriage returns to the compiler.)
const std::string GoodVertex =
    "#version 450\nlayout(location = 0) in vec2 aPosition;\nvoid main() { gl_Position = "
    "vec4(aPosition, 0.0, 1.0); }\n";

const std::string GoodFragment =
    "#version 450\nlayout(location = 0) out vec4 fragColor;\nvoid main() { fragColor = "
    "vec4(1.0); }\n";

const std::string BrokenVertex = "#version 450\nvoid main() { this is not glsl }\n";

// A real family's info: the emitters read its name for their diagnostics.
const PrecompiledShaderInfo* TestFamilyInfo() {
  for (auto factory : ShaderRegistry::All()) {
    auto info = factory()->info();
    if (info.name == "SolidColorFillShader") {
      static PrecompiledShaderInfo stored = info;
      return &stored;
    }
  }
  return nullptr;
}

EmitInput InputOf(const std::string& vertSource, const std::string& fragSource,
                  Diagnostics* diagnostics = nullptr) {
  EmitInput input;
  input.diagnostics = diagnostics;
  input.info = TestFamilyInfo();
  input.vertIndex = 0;
  input.fragIndex = 0;
  input.vertSource = &vertSource;
  input.fragSource = &fragSource;
  static const std::vector<std::string> noDefines;
  input.vertDefines = &noDefines;
  input.fragDefines = &noDefines;
  static const std::vector<uint32_t> noSpirv;
  input.vertSpirv = &noSpirv;
  static const SpirvStage noStage;
  input.fragStage = &noStage;
  return input;
}

}  // namespace

TEST(ProfileEmitterTest, TheVulkanEmitterEmitsACompilableVariant) {
  StageCache cache;  // Memory only: the test never opens a directory.
  StageCompiler compiler(&cache);
  auto emitter = MakeProfileEmitter(ShaderProfile::Vulkan);
  ASSERT_NE(emitter, nullptr);
  EXPECT_EQ(emitter->profile(), ShaderProfile::Vulkan);
  auto input = InputOf(GoodVertex, GoodFragment);
  auto stages = emitter->emit(input, &compiler);
  ASSERT_TRUE(stages.has_value());
  EXPECT_FALSE(stages->vertexBlob.empty());
  EXPECT_FALSE(stages->fragmentBlob.empty());
}

TEST(ProfileEmitterTest, AnOptimizationFailureFailsTheVariantInsteadOfFallingBack) {
  // The optimized compile of a broken source fails; the emitter must fail the variant rather
  // than store the unoptimized module: the build would silently ship something else than what
  // every other variant ships.
  StageCache cache;
  StageCompiler compiler(&cache);
  auto emitter = MakeProfileEmitter(ShaderProfile::Vulkan);
  ASSERT_NE(emitter, nullptr);
  Diagnostics diagnostics;
  auto input = InputOf(BrokenVertex, GoodFragment, &diagnostics);
  auto stages = emitter->emit(input, &compiler);
  EXPECT_FALSE(stages.has_value());
  ASSERT_FALSE(diagnostics.records().empty());
  EXPECT_STREQ(diagnostics.records()[0].code, DiagnosticCode::EmitFailed);
  EXPECT_EQ(diagnostics.records()[0].layer, Layer::Emit);
  EXPECT_EQ(diagnostics.records()[0].profile, "vulkan");
  EXPECT_NE(diagnostics.records()[0].message.find("optimized SPIR-V compilation failed"),
            std::string::npos);
}

TEST(ProfileEmitterTest, EveryProfileHasAnEmitter) {
  for (auto profile : AllShaderProfiles) {
    auto emitter = MakeProfileEmitter(profile);
    ASSERT_NE(emitter, nullptr) << ProfileTag(profile);
    EXPECT_EQ(emitter->profile(), profile);
  }
}

}  // namespace tgfx

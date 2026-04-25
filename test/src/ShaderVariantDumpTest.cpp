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

#include <sstream>
#include <unordered_map>
#include "../../tools/shader_variant_dump/VariantDumper.h"
#include "gpu/ShaderMacroSet.h"
#include "gpu/ShaderModuleRegistry.h"
#include "gpu/processors/PorterDuffXferProcessor.h"
#include "gpu/variants/ShaderVariant.h"
#include "gtest/gtest.h"
#include "tgfx/core/BlendMode.h"

namespace tgfx {

// 1) Dumping every registered processor completes without crashing and covers the expected
//    inventory size: 33 processors and 2208 variants (the number established by ShaderVariantTest
//    aggregate checks).
TEST(ShaderVariantDumpTest, DumpCompletes) {
  std::stringstream out;
  ASSERT_NO_FATAL_FAILURE(DumpAllVariants(out));
  auto json = out.str();
  EXPECT_FALSE(json.empty());
  EXPECT_NE(json.find("\"totalVariants\":2208"), std::string::npos)
      << "JSON did not include the expected totalVariants count. First 256 chars:\n"
      << json.substr(0, 256);
  EXPECT_NE(json.find("\"processorCount\":33"), std::string::npos)
      << "JSON did not include the expected processorCount.";
}

// 2) Byte-level equivalence: for every processor the dumper knows about, each ShaderVariant's
//    preamble returned by EnumerateVariants() must match byte-for-byte what the runtime path
//    (onBuildShaderMacros -> toPreamble) produces. For processors with a static BuildMacros we
//    directly invoke it with the variant parameters reverse-derived from the variant index; the
//    expectation is that EnumerateVariants walks the same Cartesian product, so the preambles
//    coincide by construction. We still exercise it explicitly for PorterDuffXferProcessor
//    because its 60-variant space is the largest non-trivial surface in the project and makes
//    regressions most visible.
TEST(ShaderVariantDumpTest, PorterDuffXP_PreambleMatchesBuildMacros) {
  auto variants = PorterDuffXferProcessor::EnumerateVariants();
  ASSERT_EQ(variants.size(), 60u);
  // EnumerateVariants iterates blendMode in the outer loop and hasDstTexture in the inner loop
  // (see PorterDuffXferProcessor::EnumerateVariants). Reproduce that ordering here so we can
  // reverse-map index -> (blendMode, hasDstTexture) without duplicating the enumeration logic.
  for (const auto& variant : variants) {
    int modeInt = variant.index / 2;
    int hasDstInt = variant.index % 2;
    ShaderMacroSet macros;
    PorterDuffXferProcessor::BuildMacros(static_cast<BlendMode>(modeInt), hasDstInt != 0, macros);
    auto expectedPreamble = macros.toPreamble();
    EXPECT_EQ(variant.preamble, expectedPreamble)
        << "Preamble mismatch for variant index=" << variant.index << " name=" << variant.name;
  }
}

// 3) Every processor entry the dumper exposes must, when present in ShaderModuleRegistry, yield a
//    non-empty GLSL source. The only permitted exception is ComposeFragmentProcessor, which is a
//    pure structural container without a GLSL module. This guards against future processors being
//    added to the dumper list but forgetting a module registration.
TEST(ShaderVariantDumpTest, EveryProcessorHasModuleSourceExceptCompose) {
  const auto& entries = AllVariantProcessors();
  for (const auto& entry : entries) {
    const std::string& key = entry.moduleLookupKey.empty() ? entry.name : entry.moduleLookupKey;
    if (entry.name == "ComposeFragmentProcessor") {
      EXPECT_FALSE(ShaderModuleRegistry::HasModule(key))
          << "ComposeFragmentProcessor unexpectedly has a module registered.";
      continue;
    }
    ASSERT_TRUE(ShaderModuleRegistry::HasModule(key))
        << "Processor '" << entry.name << "' (lookup key '" << key
        << "') is missing a ShaderModuleRegistry entry.";
    auto id = ShaderModuleRegistry::GetModuleID(key);
    const auto& source = ShaderModuleRegistry::GetModule(id);
    EXPECT_FALSE(source.empty()) << "Processor '" << entry.name << "' has an empty module source.";
  }
}

// 4) All ShaderVariant names within a processor must be unique so downstream tooling can use
//    `{processor, variantName}` as a stable primary key. (The existing ShaderVariantTest only
//    enforces hash uniqueness; names might still collide even when hashes differ.)
TEST(ShaderVariantDumpTest, VariantNamesUniquePerProcessor) {
  const auto& entries = AllVariantProcessors();
  for (const auto& entry : entries) {
    auto variants = entry.enumerate();
    std::unordered_map<std::string, int> seen;
    for (const auto& variant : variants) {
      auto existing = seen.emplace(variant.name, variant.index);
      EXPECT_TRUE(existing.second)
          << "Processor '" << entry.name << "' has duplicate variant name '" << variant.name
          << "' at indices " << existing.first->second << " and " << variant.index;
    }
  }
}

}  // namespace tgfx
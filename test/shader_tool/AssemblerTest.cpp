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
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. See the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

// The assembler's 2a capabilities, pinned at the text level: the specialization prologue (the
// master switch, one constant per declared id, the per-field accessors) and the unrolled executor
// macro follow the declaration, and a family without spec elements keeps a byte-clean text.

#include <string>

#include "TestDeclaredModules.h"
#include "emit/Assembler.h"
#include "gpu/shaders/Families.h"
#include "gtest/gtest.h"

namespace tgfx {
namespace {

bool Contains(const std::string& text, const char* fragment) {
  return text.find(fragment) != std::string::npos;
}

}  // namespace

TEST(AssemblerTest, GeneratesTheSpecPrologueFromTheDeclaration) {
  const auto assembled = AssembleVariant(test_modules::TestFamily, test_modules::TestVariant(1));
  const auto& frag = assembled.fragText;
  // The master switch first, then the scalar field's single id, then the Int4 pool's ids.
  EXPECT_TRUE(Contains(frag, "layout(constant_id = 0) const int TGFX_SPEC_0 = 0;")) << frag;
  EXPECT_TRUE(Contains(frag, "layout(constant_id = 1) const int TGFX_SPEC_1 = 0;")) << frag;
  EXPECT_TRUE(Contains(frag, "layout(constant_id = 2) const int TGFX_SPEC_2 = 0;")) << frag;
  // 32 elements * 4 ids = ids 2..129.
  EXPECT_TRUE(Contains(frag, "layout(constant_id = 129) const int TGFX_SPEC_129 = 0;")) << frag;
  // The scalar accessor: the constant under the master switch, the uniform otherwise.
  EXPECT_TRUE(Contains(frag, "int TestSpec_SPEC_SlotCount() { int s = TGFX_SPEC_0; if (s == 1) { "
                             "return TGFX_SPEC_1; } return TestSpec_SlotCount; }"))
      << frag;
  // The Int4 accessor indexes the constants by literal element and falls back to the array.
  EXPECT_TRUE(Contains(frag, "ivec4 TestSpec_SPEC_SlotPacked(int i) {")) << frag;
  EXPECT_TRUE(Contains(frag, "return TestSpec_SlotPacked[i];")) << frag;
  // The spec fields stay uniform block members: the generic compile and the reflection read them.
  EXPECT_TRUE(Contains(frag, "ivec4 TestSpec_SlotPacked[32];")) << frag;
  // The vertex stage carries no spec text (spec elements are fragment params by contract).
  EXPECT_FALSE(Contains(assembled.vertText, "TGFX_SPEC")) << assembled.vertText;
}

TEST(AssemblerTest, UnrollSitesFollowTheVariantCapacity) {
  const auto wide = AssembleVariant(test_modules::TestFamily, test_modules::TestVariant(1));
  EXPECT_TRUE(Contains(wide.fragText, "#define TestSpec_RUN_UNROLLED_SLOTS()")) << wide.fragText;
  EXPECT_TRUE(Contains(wide.fragText, "if (TestSpec_SPEC_SlotCount() > 0) { runSlotAt(0, result); }"))
      << wide.fragText;
  EXPECT_TRUE(
      Contains(wide.fragText, "if (TestSpec_SPEC_SlotCount() > 31) { runSlotAt(31, result); }"))
      << wide.fragText;

  // The 3-slot capacity emits three sites only: the capacity is a declaration fact, so no
  // preprocessor guard is needed (the former CHAIN_SLOT_CAPACITY #if brackets).
  const auto tail = AssembleVariant(test_modules::TestFamily, test_modules::TestVariant(2));
  EXPECT_TRUE(
      Contains(tail.fragText, "if (TestSpec_SPEC_SlotCount() > 2) { runSlotAt(2, result); }"))
      << tail.fragText;
  EXPECT_FALSE(Contains(tail.fragText, "runSlotAt(3,")) << tail.fragText;
}

TEST(AssemblerTest, FamiliesWithoutSpecKeepACleanText) {
  uint16_t solidColor = 0xFFFF;
  for (size_t index = 0; index < FamilyCount(); ++index) {
    if (std::string(Families()[index].name) == "SolidColor") {
      solidColor = static_cast<uint16_t>(index);
    }
  }
  ASSERT_NE(solidColor, 0xFFFF);
  Variant variant = {};
  variant.family = solidColor;
  const auto assembled = AssembleVariant(Families()[solidColor], variant);
  EXPECT_FALSE(Contains(assembled.fragText, "TGFX_SPEC")) << assembled.fragText;
  EXPECT_FALSE(Contains(assembled.fragText, "RUN_UNROLLED_SLOTS")) << assembled.fragText;
}

}  // namespace tgfx

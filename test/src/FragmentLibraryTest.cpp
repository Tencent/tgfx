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

// The fragment table and the uniform allocation are the assembler's two contracts: the table must
// cover every chain operator with the same parameter vocabulary ChainOps.h declares, and the
// allocator must give one slot per semantic in a deterministic order. These tests pin both.

#include "base/TGFXTest.h"
#include "gpu/shaders/contract/ChainISA.h"
#include "gpu/shaders/contract/ChainOps.h"
#include "gpu/shaders/contract/FragmentLibrary.h"
#include "gpu/shaders/contract/UniformAllocation.h"

namespace tgfx {
namespace {

uint32_t WordsOf(const std::vector<FragmentParam>& params) {
  uint32_t words = 0;
  for (const auto& param : params) {
    words += FragmentTypeSize(param.type) / 16 * param.arraySize;
  }
  return words;
}

}  // namespace

TGFX_TEST(FragmentLibraryTest, EveryChainOperatorHasAFragment) {
  for (int op = 0; op < ChainISA::OpCount; ++op) {
    const auto* fragment = FindFragment(op);
    ASSERT_NE(fragment, nullptr) << "op " << op << " has no fragment";
    if (fragment->kind == FragmentKind::Function || fragment->kind == FragmentKind::EntryCoupled) {
      EXPECT_NE(fragment->functionName[0], '\0');
    }
  }
  EXPECT_EQ(FragmentCount(), static_cast<size_t>(ChainISA::OpCount));
}

TGFX_TEST(FragmentLibraryTest, ParamTablesMatchTheChainOpsVocabulary) {
  // The word counts under the new packing (whole vec4 words): Luma's three bare floats share one
  // word instead of the pool's three, every other operator's vocabulary maps one to one.
  EXPECT_EQ(WordsOf(FindFragment(ChainISA::ColorMatrix)->params), 5u);
  EXPECT_EQ(WordsOf(FindFragment(ChainISA::Luma)->params), 1u);
  EXPECT_EQ(WordsOf(FindFragment(ChainISA::AlphaThreshold)->params), 1u);
  EXPECT_EQ(WordsOf(FindFragment(ChainISA::ConstColor)->params), 1u);
  EXPECT_EQ(WordsOf(FindFragment(ChainISA::AARectCoverage)->params), 1u);
  EXPECT_EQ(WordsOf(FindFragment(ChainISA::LocalRectCoverage)->params), 4u);
  EXPECT_EQ(WordsOf(FindFragment(ChainISA::RRectCoverage)->params), 7u);
}

TGFX_TEST(FragmentLibraryTest, AllocatorDedupsSemanticsAndKeepsOrder) {
  UniformAllocator allocator;
  const auto& color = allocator.Allocate("ConstColor.color", FragmentType::Float4);
  EXPECT_EQ(color.offset, 0u);
  EXPECT_EQ(color.size, 16u);
  const auto& again = allocator.Allocate("ConstColor.color", FragmentType::Float4);
  EXPECT_EQ(&again, &color);
  EXPECT_EQ(allocator.BlockSize(), 16u);

  const auto& matrix = allocator.Allocate("ColorMatrix.linear", FragmentType::Matrix4);
  EXPECT_EQ(matrix.offset, 16u);
  EXPECT_EQ(matrix.size, 64u);
  const auto& deviceToLocal = allocator.Allocate("Clip.deviceToLocal", FragmentType::Matrix3);
  EXPECT_EQ(deviceToLocal.size, 48u);
  EXPECT_EQ(allocator.Bindings().size(), 3u);
  EXPECT_EQ(allocator.BlockSize(), 16u + 64u + 48u);
}

TGFX_TEST(FragmentLibraryTest, ProgramLevelFlagIsRecordedAndOffsetsStayMonotonic) {
  UniformAllocator allocator;
  allocator.Allocate("ConstColor.color", FragmentType::Float4);
  allocator.Allocate("Gradient.block", FragmentType::Float4, 18, /*programLevel=*/true);
  allocator.Allocate("ColorMatrix.linear", FragmentType::Matrix4);
  ASSERT_EQ(allocator.Bindings().size(), 3u);
  EXPECT_EQ(allocator.Bindings()[0].offset, 0u);
  EXPECT_EQ(allocator.Bindings()[1].offset, 16u);
  EXPECT_EQ(allocator.Bindings()[2].offset, 16u + 18u * 16u);
  EXPECT_TRUE(allocator.Bindings()[1].programLevel);
  EXPECT_FALSE(allocator.Bindings()[0].programLevel);
}

}  // namespace tgfx

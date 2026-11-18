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

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "gpu/shaders/contract/FragmentLibrary.h"

namespace tgfx {

/// One allocated parameter: where a semantic landed. The offset is in bytes inside the fragment
/// uniform block; the whole table is the single numbering the shader declaration and the CPU
/// write plan read.
struct ParamSlot {
  const char* semantic = "";
  FragmentType type = FragmentType::Float;
  uint16_t arraySize = 1;
  /// Byte offset inside the fragment uniform block.
  uint32_t offset = 0;
  /// The std140 footprint in bytes (arraySize already folded in).
  uint32_t size = 0;
  /// Program-level semantics allocate ahead of the per-slot ones (see Allocate).
  bool programLevel = false;
};

/// Allocates a composed program's fragment uniform block from the fragments it uses: one slot per
/// distinct semantic (a semantic two fragments share is written once — same source, same value),
/// whole-vec4 words like the data pool's rule. The table follows the composition walk's order —
/// the one deterministic input — so the same walk always yields the same offsets. This supersedes
/// the interpreter's fixed ChainData pool: each program's block holds exactly the parameters its
/// code reads.
class UniformAllocator {
 public:
  /// Allocates (or returns the existing slot of) one semantic.
  const ParamSlot& Allocate(const char* semantic, FragmentType type, uint16_t arraySize = 1,
                            bool programLevel = false);

  /// The allocation table in offset order — the CPU write plan.
  const std::vector<ParamSlot>& Bindings() const;

  /// The block's total size in bytes (a multiple of 16).
  uint32_t BlockSize() const;

 private:
  std::vector<ParamSlot> slots = {};
};

/// The std140 footprint of one element of a type, in bytes (whole vec4 words).
uint32_t FragmentTypeSize(FragmentType type);

}  // namespace tgfx

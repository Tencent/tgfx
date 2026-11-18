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

#include "gpu/shaders/contract/UniformAllocation.h"
#include <cstring>

namespace tgfx {

uint32_t FragmentTypeSize(FragmentType type) {
  switch (type) {
    case FragmentType::Matrix3:
      return 48;
    case FragmentType::Matrix4:
      return 64;
    default:
      return 16;
  }
}

const ParamSlot& UniformAllocator::Allocate(const char* semantic, FragmentType type,
                                            uint16_t arraySize, bool programLevel) {
  for (const auto& slot : slots) {
    if (std::strcmp(slot.semantic, semantic) == 0) {
      return slot;
    }
  }
  // Appending keeps the table in the composition walk's order, which is the one deterministic
  // input: every program allocates its own block, so a slot's offset only has to be stable for
  // the same walk, never across programs.
  ParamSlot slot = {};
  slot.semantic = semantic;
  slot.type = type;
  slot.arraySize = arraySize;
  slot.offset = BlockSize();
  slot.size = FragmentTypeSize(type) * arraySize;
  slot.programLevel = programLevel;
  slots.push_back(slot);
  return slots.back();
}

const std::vector<ParamSlot>& UniformAllocator::Bindings() const {
  return slots;
}

uint32_t UniformAllocator::BlockSize() const {
  uint32_t size = 0;
  for (const auto& slot : slots) {
    size += slot.size;
  }
  return size;
}

}  // namespace tgfx

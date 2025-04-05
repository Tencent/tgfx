/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#pragma once

#include "BlockBuffer.h"
#include "core/utils/PlacementPtr.h"

namespace tgfx {
/**
 * The PlacementBuffer class allows creating objects in pre-allocated memory blocks. This helps
 * reduce allocation overhead when creating many small objects. All objects created by the
 * PlacementBuffer must be destroyed before the PlacementBuffer itself is cleared or destroyed.
 */
class PlacementBuffer {
 public:
  /**
   * Constructs a PlacementBuffer with the given initial block size.
   */
  PlacementBuffer(size_t initBlockSize = 1024) : blockBuffer(initBlockSize) {
  }

  PlacementBuffer(const PlacementBuffer&) = delete;
  PlacementBuffer& operator=(const PlacementBuffer&) = delete;

  /**
   * Creates an object of the given type in the PlacementBuffer. Returns a PlacementPtr to wrap the
   * created object. Returns nullptr if the allocation fails.
   */
  template <typename T, typename... Args>
  PlacementPtr<T> make(Args&&... args) {
    void* memory = blockBuffer.allocate(sizeof(T));
    if (!memory) {
      return nullptr;
    }
    return PlacementPtr<T>(new (memory) T(std::forward<Args>(args)...));
  }

  /**
   * Returns the total size of all created objects in bytes.
   */
  size_t size() const {
    return blockBuffer.size();
  }

  /**
   * Resets the size to zero to reuse the memory blocks. If maxReuseSize is specified, blocks at the
   * end of the list that exceed this size will be freed.
   */
  void clear(size_t maxReuseSize = std::numeric_limits<size_t>::max()) {
    blockBuffer.clear(maxReuseSize);
  }

 private:
  BlockBuffer blockBuffer = {};
};

}  // namespace tgfx

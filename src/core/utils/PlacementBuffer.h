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

#include <cstdint>
#include <limits>
#include <vector>
#include "core/utils/PlacementNode.h"
#include "core/utils/PlacementPtr.h"

namespace tgfx {
/**
 * The PlacementBuffer class allows creating objects in pre-allocated memory blocks. This helps
 * reduce allocation overhead when creating many small objects. All objects created by the
 * PlacementBuffer must be destroyed before the PlacementBuffer itself is cleared or destroyed.
 */
class PlacementBuffer {
 public:
  explicit PlacementBuffer(size_t initBlockSize = 1024);

  ~PlacementBuffer();

  /**
   * Creates an object of the given type in the PlacementBuffer. Returns a PlacementPtr to wrap the
   * created object. Returns nullptr if the allocation fails.
   */
  template <typename T, typename... Args>
  PlacementPtr<T> make(Args&&... args) {
    void* memory = allocate(sizeof(T));
    if (!memory) {
      return nullptr;
    }
    return PlacementPtr<T>(new (memory) T(std::forward<Args>(args)...));
  }

  /**
   * Creates a PlacementNode of the specified type in the PlacementBuffer. The node can then be
   * added to a PlacementList. Returns nullptr if the allocation fails.
   */
  template <typename T, typename... Args>
  PlacementNode<T> makeNode(Args&&... args) {
    using Storage = typename PlacementNode<T>::Storage;
    void* memory = alignedAllocate(PlacementNode<T>::ALIGNMENT, sizeof(Storage));
    if (!memory) {
      return nullptr;
    }
    auto storage = static_cast<Storage*>(memory);
    storage->next = nullptr;
    new (storage->memory) T(std::forward<Args>(args)...);
    return PlacementNode<T>(storage);
  }

  /**
   * Allocates memory for an object of the given size. Returns nullptr if the allocation fails.
   */
  void* allocate(size_t size);

  /**
   * Allocates memory for an object of the given size with the given alignment. Returns nullptr if
   * the allocation fails.
   */
  void* alignedAllocate(size_t alignment, size_t size);

  /**
   * Returns the total size of all created objects in bytes.
   */
  size_t size() const {
    return usedSize;
  }

  /**
   * Resets the size to zero and clears all allocated blocks, but keeps the last block for reuse if
   * it is not larger than maxReuseSize.
   */
  void clear(size_t maxReuseSize = std::numeric_limits<size_t>::max());

 private:
  size_t initBlockSize = 0;
  size_t currentBlockIndex = 0;
  size_t usedSize = 0;

  struct Block {
    Block() = default;

    Block(uint8_t* data, size_t size) : data(data), size(size) {
    }

    uint8_t* data = nullptr;
    size_t size = 0;
    size_t offset = 0;
  };

  std::vector<Block> blocks = {};

  Block* findOrAllocateBlock(size_t requestedSize);
  bool allocateNewBlock(size_t requestSize);
};

}  // namespace tgfx

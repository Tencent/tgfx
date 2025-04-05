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
#include "core/utils/PlacementPtr.h"

namespace tgfx {
/**
 * A buffer that allocates memory in blocks. This can be used to allocate many small objects in
 * shared memory blocks to reduce the overhead of memory allocation. All objects created in the
 * BlockBuffer must be destroyed before the BlockBuffer itself is cleared or destroyed.
 */
class BlockBuffer {
 public:
  /**
   * Constructs a BlockBuffer with the given initial block size.
   */
  BlockBuffer(size_t initBlockSize = 1024);

  BlockBuffer(const BlockBuffer&) = delete;
  BlockBuffer& operator=(const BlockBuffer&) = delete;

  /**
   * Destroys the BlockBuffer and frees all allocated memory blocks.
   */
  virtual ~BlockBuffer();

  /**
   * Creates an object of the given type in the BlockBuffer. Returns a PlacementPtr to wrap the
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
 * Allocates memory for an object of the given size. Returns nullptr if the allocation fails.
 */
  void* allocate(size_t size);

  /**
   * Allocates memory for an object of the given size with the given alignment. Returns nullptr if
   * the allocation fails.
   */
  void* allocate(size_t size, size_t alignment);

  /**
   * Returns the total size of all allocated memory.
   */
  size_t size() const {
    return usedSize;
  }

  /**
   * Returns the address and size of the current memory block.
   */
  std::pair<const void*, size_t> currentBlock() const;

  /**
   * Resets the size to zero to reuse the memory blocks. If maxReuseSize is specified, blocks at the
   * end of the list that exceed this size will be freed.
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

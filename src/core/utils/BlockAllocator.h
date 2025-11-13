/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <vector>
#include "core/utils/PlacementArray.h"

namespace tgfx {

/**
 * BlockBuffer is a helper class that manages the memory blocks released from a BlockAllocator. It
 * is responsible for freeing the memory blocks when they are no longer needed.
 */
class BlockBuffer {
 public:
  explicit BlockBuffer(std::vector<uint8_t*> blocks);

  ~BlockBuffer();

  /**
   * Shrinks the last memory block to the specified size. Returns a pointer to the resized block.
   */
  void* shrinkLastBlockTo(size_t newSize);

 private:
  std::vector<uint8_t*> blocks = {};
};

/**
 * An allocator that allocates memory in blocks. This can be used to allocate many small objects in
 * shared memory blocks to reduce the overhead of memory allocation. All objects created in the
 * BlockAllocator must be destroyed before the BlockAllocator itself is cleared or destroyed.
 */
class BlockAllocator {
 public:
  /**
   * Constructs a BlockAllocator with the given initial and maximum block sizes.
   * @param initBlockSize The initial size of each memory block. Must be greater than zero.
   * @param maxBlockSize The maximum size for any memory block. If this is too small, larger blocks
   * may still be allocated as needed.
   */
  explicit BlockAllocator(size_t initBlockSize, size_t maxBlockSize = SIZE_MAX);

  /**
   * Constructs a BlockAllocator with a default initial block size of 256 bytes and no maximum block
   * size limit.
   */
  BlockAllocator() : BlockAllocator(256, SIZE_MAX) {
  }

  BlockAllocator(const BlockAllocator&) = delete;
  BlockAllocator& operator=(const BlockAllocator&) = delete;

  /**
   * Destroys the BlockAllocator and frees all allocated memory blocks.
   */
  ~BlockAllocator();

  /**
   * Creates an object of the given type in the BlockAllocator. Returns a PlacementPtr to wrap the
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
   * Creates a new PlacementArray with the specified count, initializing all elements to nullptr.
   * If the count is zero, an empty PlacementArray is returned.
   */
  template <typename T>
  PlacementArray<T> makeArray(size_t count) {
    if (count == 0) {
      return {};
    }
    auto byteSize = sizeof(PlacementPtr<T>) * count;
    void* memory = allocate(byteSize);
    memset(memory, 0, byteSize);
    return PlacementArray<T>(reinterpret_cast<PlacementPtr<T>*>(memory), count);
  }

  /**
   * Moves a list of PlacementPtr pointers into a new PlacementArray. The original pointers are
   * released, and the new array takes ownership of the elements. Returns a PlacementArray with the
   * moved elements.
   */
  template <typename T, typename U>
  PlacementArray<T> makeArray(PlacementPtr<U>* elements, size_t count) {
    static_assert(std::is_base_of_v<T, U>, "U must be a subclass of T!");
    if (count == 0) {
      return {};
    }
    auto byteSize = sizeof(PlacementPtr<T>) * count;
    void* memory = allocate(byteSize);
    memcpy(memory, elements, byteSize);
    memset(static_cast<void*>(elements), 0, byteSize);
    return PlacementArray<T>(reinterpret_cast<PlacementPtr<T>*>(memory), count);
  }

  /**
   * Moves the elements from the given PlacementPtr vector into a new PlacementArray. The original
   * pointers are released, and the new array takes ownership of the elements. Returns a
   * PlacementArray containing the moved elements.
   */
  template <typename T>
  PlacementArray<T> makeArray(std::vector<PlacementPtr<T>>&& vector) {
    auto array = makeArray<T>(vector.data(), vector.size());
    vector.clear();
    return array;
  }

  /**
   * Allocates memory for an object of the given size. Returns nullptr if the allocation fails.
   */
  void* allocate(size_t size);

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

  /**
   * Transfers ownership of the memory blocks to the returned BlockBuffer object and resets this
   * BlockAllocator to its initial state. Returns nullptr if the BlockAllocator size is zero and
   * leaves it unchanged.
   */
  std::unique_ptr<BlockBuffer> release();

  /**
   * Returns a shared pointer that acts as a reference counter for this BlockAllocator.
   * Asynchronous objects using the BlockAllocator can hold this shared pointer. When all references
   * are released, the BlockAllocator will be notified.
   */
  std::shared_ptr<BlockAllocator> addReference();

 private:
  struct Block {
    Block() = default;

    Block(uint8_t* data, size_t size) : data(data), size(size) {
    }

    uint8_t* data = nullptr;
    size_t size = 0;
    size_t offset = 0;
  };

  std::mutex mutex = {};
  std::condition_variable condition = {};
  std::weak_ptr<BlockAllocator> externalReferences;
  std::vector<Block> blocks = {};
  size_t initBlockSize = 0;
  size_t maxBlockSize = SIZE_MAX;
  size_t currentBlockIndex = 0;
  size_t usedSize = 0;

  /**
   * Notifies when the reference count drops to zero. Called by the destructor of the reference
   * counter shared pointer.
   */
  static void NotifyReferenceReachedZero(BlockAllocator* allocator);

  /**
   * Waits until all references are released, ensuring that all threads have finished using the
   * memory blocks, and it is safe to free them.
   */
  void waitForReferencesExpired();
  Block* findOrAllocateBlock(size_t requestedSize);
  bool allocateNewBlock(size_t requestSize);

  size_t nextBlockSize(size_t currentSize) const {
    return std::min(currentSize * 2, maxBlockSize);
  }
};
}  // namespace tgfx

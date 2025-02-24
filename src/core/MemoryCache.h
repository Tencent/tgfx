/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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
#include <cstddef>

namespace tgfx {
class MemoryCache final {
 public:
  explicit MemoryCache(size_t blockIncrementBytes = kMinAllocationSize);

  MemoryCache(const MemoryCache&) = delete;

  MemoryCache& operator=(const MemoryCache&) = delete;

  MemoryCache(MemoryCache&&) = delete;

  MemoryCache& operator=(MemoryCache&&) = delete;

  ~MemoryCache();

  void* allocate(size_t size);

  void release(const void* ptr) const;

  void resetCache();

  size_t memoryBlockIncrementBytes() const {
    return blockIncrementBytes;
  }

  size_t memoryBlockNum() const {
    return blockNum;
  }

  class Block final {
   public:
    explicit Block(size_t allocationSize) : size(allocationSize), cursor(kBlockStart) {
    }

    ~Block() = default;

    void* ptr(size_t offset) {
      return reinterpret_cast<char*>(this) + offset;
    }

    const void* ptr(size_t offset) const {
      return const_cast<Block*>(this)->ptr(offset);
    }

    void ref() {
      ++refCount;
    }
    void unref() {
      --refCount;
    }

    void reset() {
      cursor = kBlockStart;
      refCount = 0;
    }

   private:
    Block* next = nullptr;
    size_t size = 0;
    size_t cursor = 0;
    size_t refCount = 0;

    friend class MemoryCache;
  };

  static constexpr size_t kBlockStart = sizeof(Block);

 private:
  // smallest block size allocated on the heap, 4096 bytes.
  static constexpr size_t kMinAllocationSize = 1 << 12;

  size_t blockIncrementBytes = kMinAllocationSize;
  size_t blockNum = 0;
  Block* head = nullptr;
  Block* tail = nullptr;
  Block* current = nullptr;

  Block* findOwnerBlock(const void* ptr) const;

  Block* createNewBlock(size_t size) const;

  static size_t AlignedAllocSize(size_t size);
};
}  // namespace tgfx

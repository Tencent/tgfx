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

#include "MemoryCache.h"

#include <memory>

namespace tgfx {
MemoryCache::MemoryCache(size_t blockIncrementBytes) {
  blockIncrementBytes = blockIncrementBytes < kMinAllocationSize ? kMinAllocationSize : blockIncrementBytes;
  this->blockIncrementBytes = AlignedAllocSize(blockIncrementBytes);
  head = createNewBlock(this->blockIncrementBytes);
  tail = head;
  current = head;
  blockNum++;
}

MemoryCache::~MemoryCache() {
  auto *block = head;
  while (block != nullptr) {
    const auto next = block->next;
    free(block);
    block = next;
  }
}

void *MemoryCache::allocate(size_t size) {
  if (size == 0) {
    return nullptr;
  }

  size_t offset = current->cursor;
  size_t endCursor = offset + size;
  if (endCursor > current->size) {
    auto *block = current->next;
    while (block != nullptr) {
      offset = block->cursor;
      endCursor = offset + size;
      if (endCursor <= block->size) {
        current = block;
        break;
      }
      block = block->next;
    }

    if (block == nullptr) {
      block = createNewBlock(size);
      current->next = block;
      current = block;
      blockNum++;

      offset = current->cursor;
      endCursor = offset + size;
    }
  }

  current->cursor = endCursor;
  current->ref();

  return current->ptr(offset);
}

void MemoryCache::release(const void *ptr) const {
  if (ptr == nullptr) {
    return;
  }

  auto *block = findOwnerBlock(ptr);
  if (block == nullptr) {
    return;
  }
  block->unref();
}

void MemoryCache::resetCache() {
  Block *block = head;
  while (block != nullptr) {
    block->reset();
    block = block->next;
  }
  current = head;
}

MemoryCache::Block *MemoryCache::findOwnerBlock(const void *ptr) const {
  auto *block = head;
  while (block != nullptr) {
    const auto start = reinterpret_cast<uintptr_t>(block->ptr(0));
    const auto current = reinterpret_cast<uintptr_t>(ptr);
    if (current >= start && current < start + block->size) {
      return block;
    }
    block = block->next;
  }
  return nullptr;
}

MemoryCache::Block *MemoryCache::createNewBlock(size_t size) const {
  size_t allocSize = size < blockIncrementBytes ? blockIncrementBytes : AlignedAllocSize(size);
  void *buffer = malloc(allocSize);
  auto *block = new(buffer) Block(allocSize);
  return block;
}

size_t MemoryCache::AlignedAllocSize(size_t size) {
  // if alloc size > 32KB, aligns on 4KB boundary otherwise aligns on std::max_align_t
  const size_t mask = size > (1 << 15) ? ((1 << 12) - 1) : (alignof(std::max_align_t) - 1);
  return (size + mask) & ~mask;
}
} // tgfx

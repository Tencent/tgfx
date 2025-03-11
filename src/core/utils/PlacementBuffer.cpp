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

#include "PlacementBuffer.h"
#include "core/utils/Log.h"

namespace tgfx {
// The maximum size of a memory block that can be allocated. Allocating a block that's too large can
// cause memory fragmentation and slow down the allocation process. It might also increase the host
// application's memory usage due to pre-allocation optimizations on some platforms.
static constexpr size_t MAX_BLOCK_SIZE = 1 << 21;  // 2MB

// The alignment of memory blocks. Set to 64 bytes to ensure that the memory blocks are aligned to
// cache lines.
static constexpr size_t BLOCK_ALIGNMENT = 64;

static size_t NextBlockSize(size_t currentSize) {
  return std::min(currentSize * 2, MAX_BLOCK_SIZE);
}

PlacementBuffer::PlacementBuffer(size_t initBlockSize) : initBlockSize(initBlockSize) {
  DEBUG_ASSERT(initBlockSize > 0);
}

PlacementBuffer::~PlacementBuffer() {
  for (auto& block : blocks) {
    free(block.data);
  }
}

void* PlacementBuffer::allocate(size_t size) {
  auto block = findOrAllocateBlock(size);
  if (block == nullptr) {
    return nullptr;
  }
  void* data = block->data + block->offset;
  block->offset += size;
  usedSize += size;
  return data;
}

void* PlacementBuffer::alignedAllocate(size_t alignment, size_t size) {
  auto block = findOrAllocateBlock(size + alignment - 1);
  if (block == nullptr) {
    return nullptr;
  }
  auto baseAddress = reinterpret_cast<uintptr_t>(block->data + block->offset);
  uintptr_t alignedAddress = (baseAddress + alignment - 1) & ~(alignment - 1);
  size_t alignOffset = alignedAddress - reinterpret_cast<uintptr_t>(block->data);
  block->offset = alignOffset + size;
  usedSize += size;
  return reinterpret_cast<void*>(alignedAddress);
}

PlacementBuffer::Block* PlacementBuffer::findOrAllocateBlock(size_t requestedSize) {
  // Try to use an existing block
  while (currentBlockIndex < blocks.size()) {
    auto& block = blocks[currentBlockIndex];
    if (block.size - block.offset >= requestedSize) {
      return &block;
    }
    currentBlockIndex++;
  }
  // Otherwise allocate a new block
  if (!allocateNewBlock(requestedSize)) {
    return nullptr;
  }
  return &blocks[currentBlockIndex];
}

void PlacementBuffer::clear(size_t maxReuseSize) {
  currentBlockIndex = 0;
  usedSize = 0;
  size_t totalBlockSize = 0;
  size_t reusedBlockCount = 0;
  for (auto& block : blocks) {
    if (totalBlockSize < maxReuseSize) {
      block.offset = 0;
      totalBlockSize += block.size;
      reusedBlockCount++;
    } else {
      free(block.data);
    }
  }
  blocks.resize(reusedBlockCount);
}

bool PlacementBuffer::allocateNewBlock(size_t requestSize) {
  if (requestSize > MAX_BLOCK_SIZE) {
    LOGE("PlacementBuffer::allocateNewBlock() Request size exceeds the maximum block size: %zu ",
         requestSize);
    return false;
  }
  auto blockSize = blocks.empty() ? initBlockSize : NextBlockSize(blocks.back().size);
  while (blockSize < requestSize) {
    blockSize = NextBlockSize(blockSize);
  }
  blockSize = (blockSize + BLOCK_ALIGNMENT - 1) & ~(BLOCK_ALIGNMENT - 1);
  auto data = static_cast<uint8_t*>(aligned_alloc(BLOCK_ALIGNMENT, blockSize));
  if (data == nullptr) {
    LOGE("PlacementBuffer::allocateNewBlock() Failed to allocate memory block size: %zu",
         blockSize);
    return false;
  }
  currentBlockIndex = blocks.size();
  blocks.emplace_back(data, blockSize);
  return true;
}
}  // namespace tgfx

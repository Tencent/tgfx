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

#include "BlockBuffer.h"
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

BlockData::BlockData(std::vector<uint8_t*> blocks) : blocks(std::move(blocks)) {
  DEBUG_ASSERT(!this->blocks.empty());
}

BlockData::~BlockData() {
  for (auto& block : blocks) {
    free(block);
  }
}

BlockBuffer::BlockBuffer(size_t initBlockSize) : initBlockSize(initBlockSize) {
  DEBUG_ASSERT(initBlockSize > 0);
}

BlockBuffer::~BlockBuffer() {
  waitForReferencesExpired();
  for (auto& block : blocks) {
    free(block.data);
  }
}

void* BlockBuffer::allocate(size_t size) {
  auto block = findOrAllocateBlock(size);
  if (block == nullptr) {
    return nullptr;
  }
  void* data = block->data + block->offset;
  block->offset += size;
  usedSize += size;
  return data;
}

BlockBuffer::Block* BlockBuffer::findOrAllocateBlock(size_t requestedSize) {
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

std::pair<const void*, size_t> BlockBuffer::currentBlock() const {
  if (usedSize == 0) {
    return {nullptr, 0};
  }
  auto& block = blocks[currentBlockIndex];
  return {block.data, block.offset};
}

void BlockBuffer::clear(size_t maxReuseSize) {
  if (blocks.empty()) {
    return;
  }
  waitForReferencesExpired();
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

std::shared_ptr<BlockData> BlockBuffer::release() {
  if (usedSize == 0) {
    return nullptr;
  }
  waitForReferencesExpired();
  std::vector<uint8_t*> usedBlocks = {};
  usedBlocks.reserve(currentBlockIndex + 1);
  for (auto& block : blocks) {
    if (block.offset > 0) {
      usedBlocks.push_back(block.data);
    } else {
      free(block.data);
    }
  }
  blocks.clear();
  currentBlockIndex = 0;
  usedSize = 0;
  return std::make_shared<BlockData>(std::move(usedBlocks));
}

bool BlockBuffer::allocateNewBlock(size_t requestSize) {
  size_t blockSize;
  if (requestSize <= MAX_BLOCK_SIZE) {
    blockSize = blocks.empty() ? initBlockSize : NextBlockSize(blocks.back().size);
    while (blockSize < requestSize) {
      blockSize = NextBlockSize(blockSize);
    }
  } else {
    // Allow allocating a block larger than MAX_BLOCK_SIZE if requested.
    blockSize = requestSize;
  }
  blockSize = (blockSize + BLOCK_ALIGNMENT - 1) & ~(BLOCK_ALIGNMENT - 1);
  auto data = static_cast<uint8_t*>(malloc(blockSize));
  if (data == nullptr) {
    LOGE("BlockBuffer::allocateNewBlock() Failed to allocate memory block size: %zu", blockSize);
    return false;
  }
  currentBlockIndex = blocks.size();
  blocks.emplace_back(data, blockSize);
  return true;
}

std::shared_ptr<BlockBuffer> BlockBuffer::addReference() {
  auto reference = externalReferences.lock();
  if (reference) {
    return reference;
  }
  reference = std::shared_ptr<BlockBuffer>(this, NotifyReferenceReachedZero);
  externalReferences = reference;
  return reference;
}

void BlockBuffer::waitForReferencesExpired() {
  std::unique_lock<std::mutex> lock(mutex);
  condition.wait(lock, [this]() { return externalReferences.expired(); });
}

void BlockBuffer::NotifyReferenceReachedZero(BlockBuffer* blockBuffer) {
  std::unique_lock<std::mutex> lock(blockBuffer->mutex);
  blockBuffer->condition.notify_all();
}

}  // namespace tgfx

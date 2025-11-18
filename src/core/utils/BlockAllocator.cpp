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

#include "BlockAllocator.h"
#include "../../../include/tgfx/core/Log.h"

namespace tgfx {
// The alignment of memory blocks. Set to 64 bytes to ensure that the memory blocks are aligned to
// cache lines.
static constexpr size_t BLOCK_ALIGNMENT = 64;

BlockBuffer::BlockBuffer(std::vector<uint8_t*> blocks) : blocks(std::move(blocks)) {
  DEBUG_ASSERT(!this->blocks.empty());
}

BlockBuffer::~BlockBuffer() {
  for (auto& block : blocks) {
    free(block);
  }
}

void* BlockBuffer::shrinkLastBlockTo(size_t newSize) {
  auto& lastBlock = blocks.back();
  if (auto resizedBlock = static_cast<uint8_t*>(realloc(lastBlock, newSize))) {
    lastBlock = resizedBlock;
  }
  return lastBlock;
}

BlockAllocator::BlockAllocator(size_t initBlockSize, size_t maxBlockSize)
    : initBlockSize(initBlockSize), maxBlockSize(maxBlockSize) {
  DEBUG_ASSERT(initBlockSize > 0);
}

BlockAllocator::~BlockAllocator() {
  waitForReferencesExpired();
  for (auto& block : blocks) {
    free(block.data);
  }
}

void* BlockAllocator::allocate(size_t size) {
  auto block = findOrAllocateBlock(size);
  if (block == nullptr) {
    return nullptr;
  }
  void* data = block->data + block->offset;
  block->offset += size;
  usedSize += size;
  return data;
}

BlockAllocator::Block* BlockAllocator::findOrAllocateBlock(size_t requestedSize) {
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

std::pair<const void*, size_t> BlockAllocator::currentBlock() const {
  if (usedSize == 0) {
    return {nullptr, 0};
  }
  auto& block = blocks[currentBlockIndex];
  return {block.data, block.offset};
}

void BlockAllocator::clear(size_t maxReuseSize) {
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

std::unique_ptr<BlockBuffer> BlockAllocator::release() {
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
  return std::make_unique<BlockBuffer>(std::move(usedBlocks));
}

bool BlockAllocator::allocateNewBlock(size_t requestSize) {
  size_t blockSize;
  if (requestSize <= maxBlockSize) {
    blockSize = blocks.empty() ? initBlockSize : nextBlockSize(blocks.back().size);
    while (blockSize < requestSize) {
      blockSize = nextBlockSize(blockSize);
    }
  } else {
    // Allow allocating a block larger than MAX_BLOCK_SIZE if requested.
    blockSize = requestSize;
  }
  blockSize = (blockSize + BLOCK_ALIGNMENT - 1) & ~(BLOCK_ALIGNMENT - 1);
  auto data = static_cast<uint8_t*>(malloc(blockSize));
  if (data == nullptr) {
    LOGE("BlockAllocator::allocateNewBlock() Failed to allocate memory block size: %zu", blockSize);
    return false;
  }
  currentBlockIndex = blocks.size();
  blocks.emplace_back(data, blockSize);
  return true;
}

std::shared_ptr<BlockAllocator> BlockAllocator::addReference() {
  auto reference = externalReferences.lock();
  if (reference) {
    return reference;
  }
  reference = std::shared_ptr<BlockAllocator>(this, NotifyReferenceReachedZero);
  externalReferences = reference;
  return reference;
}

void BlockAllocator::waitForReferencesExpired() {
  std::unique_lock<std::mutex> lock(mutex);
  condition.wait(lock, [this]() { return externalReferences.expired(); });
}

void BlockAllocator::NotifyReferenceReachedZero(BlockAllocator* allocator) {
  std::unique_lock<std::mutex> lock(allocator->mutex);
  allocator->condition.notify_all();
}

}  // namespace tgfx

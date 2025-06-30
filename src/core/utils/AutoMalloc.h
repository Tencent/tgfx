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
#include <cstdlib>
#include "core/utils/Algin.h"

/**
 * Manages an allocated block of memory.If the requested size is <= SizeRequested , then the allocation
 * will come from the stack rather than the heap.
 */
namespace tgfx {
template <size_t SizeRequested>
class AutoMalloc {
 public:
  explicit AutoMalloc() : pointer(storage), size_(SizeAlign4) {
  }

  explicit AutoMalloc(size_t size) : pointer(storage), size_(SizeAlign4) {
    reset(size);
  }

  AutoMalloc(const AutoMalloc&) = delete;

  AutoMalloc& operator=(const AutoMalloc&) = delete;

  AutoMalloc(AutoMalloc&&) = default;

  AutoMalloc& operator=(AutoMalloc&&) = default;

  ~AutoMalloc() {
    if (pointer != static_cast<void*>(storage)) {
      free(pointer);
    }
  }

  void* get() const {
    return pointer;
  }

 private:
  void* reset(size_t size) {
    size = (size < SizeAlign4) ? SizeAlign4 : size;
    bool alloc = size > size_;
    if (alloc) {
      if (pointer != static_cast<void*>(storage)) {
        free(pointer);
      }
      pointer = static_cast<void*>(malloc(size));
      size_ = size;
    }
    return pointer;
  }

  static const size_t SizeAlign4 = Align4(SizeRequested);
  void* pointer = nullptr;
  size_t size_ = SizeAlign4;
  uint32_t storage[SizeAlign4 >> 2] = {};
};
}  // namespace tgfx

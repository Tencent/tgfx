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

#include <cstddef>

namespace tgfx {
/**
 * A smart pointer that manages the lifetime of an object allocated in a pre-allocated chunk of
 * memory. The object is constructed in-place and destroyed when the pointer goes out of scope. It
 * is similar to std::unique_ptr, but it does not own the memory, meaning it does not free the
 * memory when it goes out of scope.
 */
template <typename T>
class PlacementPtr {
 public:
  explicit PlacementPtr(T* pointer = nullptr) : pointer(pointer) {
  }

  PlacementPtr(std::nullptr_t) : pointer(nullptr) {
  }

  ~PlacementPtr() {
    if (pointer) {
      pointer->~T();
    }
  }

  PlacementPtr(PlacementPtr&& other) noexcept : pointer(other.pointer) {
    other.pointer = nullptr;
  }

  PlacementPtr(const PlacementPtr&) = delete;
  PlacementPtr& operator=(const PlacementPtr&) = delete;

  PlacementPtr& operator=(PlacementPtr&& other) noexcept {
    if (this != &other) {
      if (pointer) {
        pointer->~T();
      }
      pointer = other.pointer;
      other.pointer = nullptr;
    }
    return *this;
  }

  PlacementPtr& operator=(std::nullptr_t) noexcept {
    if (pointer) {
      pointer->~T();
    }
    pointer = nullptr;
    return *this;
  }

  template <typename U>
  PlacementPtr(PlacementPtr<U>&& other) noexcept : pointer(other.pointer) {
    other.pointer = nullptr;
  }

  template <typename U>
  PlacementPtr& operator=(PlacementPtr<U>&& other) noexcept {
    if (pointer != other.pointer) {
      if (pointer) {
        pointer->~T();
      }
      pointer = other.pointer;
      other.pointer = nullptr;
    }
    return *this;
  }

  bool operator==(const PlacementPtr& other) const {
    return pointer == other.pointer;
  }

  bool operator!=(const PlacementPtr& other) const {
    return pointer != other.pointer;
  }

  explicit operator bool() const {
    return pointer != nullptr;
  }

  T* get() const {
    return pointer;
  }

  void reset(T* ptr = nullptr) {
    if (pointer) {
      pointer->~T();
    }
    pointer = ptr;
  }

  T& operator*() const {
    return *pointer;
  }

  T* operator->() const {
    return pointer;
  }

 private:
  T* pointer = nullptr;

  template <typename U>
  friend class PlacementPtr;
};
}  // namespace tgfx
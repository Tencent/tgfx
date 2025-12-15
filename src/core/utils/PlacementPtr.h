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

#include <cstddef>
#include <type_traits>

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
  /**
   * Constructs a PlacementPtr with a nullptr.
   */
  PlacementPtr() : pointer(nullptr) {
  }

  /**
   * Constructs a PlacementPtr with the given pointer. The pointer must be allocated in a
   * pre-allocated memory block.
   */
  explicit PlacementPtr(T* pointer) : pointer(pointer) {
  }

  /**
   * Constructs a PlacementPtr with a nullptr.
   */
  PlacementPtr(std::nullptr_t) : pointer(nullptr) {
  }

  /**
   * Destroys the PlacementPtr and calls the destructor of the object.
   */
  ~PlacementPtr() {
    if (pointer) {
      pointer->~T();
    }
  }

  /**
   * Constructs a PlacementPtr by moving the pointer from another PlacementPtr.
   */
  PlacementPtr(PlacementPtr&& other) noexcept : pointer(other.pointer) {
    other.pointer = nullptr;
  }

  /**
   * Constructs a PlacementPtr by moving the pointer from another PlacementPtr of a different type.
   */
  template <typename U>
  PlacementPtr(PlacementPtr<U>&& other) noexcept : pointer(other.pointer) {
    static_assert(std::is_base_of_v<T, U>, "U must be derived from T!");
    other.pointer = nullptr;
  }

  PlacementPtr(const PlacementPtr&) = delete;
  PlacementPtr& operator=(const PlacementPtr&) = delete;

  /**
   * Assigns a PlacementPtr to the PlacementPtr by moving the pointer from the other PlacementPtr.
   */
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

  /**
   * Assigns a nullptr to the PlacementPtr.
   */
  PlacementPtr& operator=(std::nullptr_t) noexcept {
    if (pointer) {
      pointer->~T();
    }
    pointer = nullptr;
    return *this;
  }

  /**
   * Assigns a PlacementPtr of a different type to the PlacementPtr by moving the pointer from the
   * other PlacementPtr.
   */
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

  /**
   * Returns true if the PlacementPtr is equal to another PlacementPtr.
   */
  bool operator==(const PlacementPtr& other) const {
    return pointer == other.pointer;
  }

  /**
   * Returns true if the PlacementPtr is not equal to another PlacementPtr.
   */
  bool operator!=(const PlacementPtr& other) const {
    return pointer != other.pointer;
  }

  /**
   * Returns true if the PlacementPtr is not equal to a nullptr.
   */
  explicit operator bool() const {
    return pointer != nullptr;
  }

  /**
   * Returns the raw pointer.
   */
  T* get() const {
    return pointer;
  }

  /**
   * Resets the pointer to nullptr and calls the destructor of the object.
   */
  void reset(T* ptr = nullptr) {
    if (pointer) {
      pointer->~T();
    }
    pointer = ptr;
  }

  /**
   * Releases the ownership of the pointer and returns the raw pointer.
   */
  T* release() {
    T* temp = pointer;
    pointer = nullptr;
    return temp;
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
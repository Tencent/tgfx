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
 * PlacementArray is a simple array-like container that uses placement new to construct objects in
 * pre-allocated memory. It is similar to std::array, but does not manage the memory itself.
 */
template <typename T>
class PlacementArray {
 public:
  /**
   * Constructs an empty PlacementArray.
   */
  PlacementArray() = default;

  ~PlacementArray() {
    clear();
  }

  PlacementArray(const PlacementArray&) = delete;

  PlacementArray& operator=(const PlacementArray&) = delete;

  /**
   * Constructs a PlacementArray by moving from another PlacementArray.
   */
  PlacementArray(PlacementArray&& other) noexcept : data(other.data), _size(other._size) {
    other.data = nullptr;
    other._size = 0;
  }

  /**
   * Constructs a PlacementArray from another PlacementArray of a different type.
   */
  template <typename U>
  PlacementArray(PlacementArray<U>&& other) noexcept : data(other.data), _size(other.size) {
    other.data = nullptr;
    other._size = 0;
  }

  /**
   * Assigns a PlacementArray by moving from another PlacementArray.
   */
  PlacementArray& operator=(PlacementArray&& other) noexcept {
    if (this != &other) {
      clear();
      data = other.data;
      _size = other._size;
      other.data = nullptr;
      other._size = 0;
    }
    return *this;
  }

  /**
   * Returns the number of elements in the PlacementArray.
   */
  size_t size() const {
    return _size;
  }

  /**
   * Returns true if the PlacementArray is empty.
   */
  bool empty() const {
    return _size == 0;
  }

  /**
   * Clears the PlacementArray, destroying all elements.
   */
  void clear() {
    if (_size > 0) {
      for (size_t i = 0; i < _size; ++i) {
        data[i]->~T();
      }
      data = nullptr;
      _size = 0;
    }
  }

  /**
   * Returns a pointer to the element at the specified index.
   */
  T* operator[](size_t index) const {
    return data[index];
  }

  /**
   * Returns a pointer to the element at the front of the PlacementArray.
   */
  T* front() const {
    return data[0];
  }

  /**
   * Returns a pointer to the element at the back of the PlacementArray.
   */
  T* back() const {
    return data[_size - 1];
  }

  /**
   * The iterator class for the PlacementArray.
   */
  class Iterator {
   public:
    Iterator(T** data, size_t index) : data(data), index(index) {
    }

    /**
     * Dereferences the iterator to get the element at the current index.
     */
    T*& operator*() const {
      return data[index];
    }

    /**
     * Increments the iterator to the next element.
     */
    const Iterator& operator++() {
      ++index;
      return *this;
    }

    /**
     * Compares two iterators for equality.
     */
    bool operator!=(const Iterator& other) const {
      return index != other.index;
    }

   private:
    T** data;
    size_t index;
  };

  /**
   * Returns an iterator to the beginning of the PlacementArray.
   */
  Iterator begin() const {
    return Iterator(data, 0);
  }

  /**
   * Returns an iterator to the end of the PlacementArray.
   */
  Iterator end() const {
    return Iterator(data, _size);
  }

 private:
  T** data = nullptr;
  size_t _size = 0;

  /**
   * Constructs a PlacementArray with the specified data and size.
   */
  PlacementArray(T** data, size_t size) : data(data), _size(size) {
  }

  friend class BlockBuffer;
};
}  // namespace tgfx

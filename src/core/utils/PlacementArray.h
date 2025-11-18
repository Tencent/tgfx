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

#include "core/utils/Log.h"
#include "core/utils/PlacementPtr.h"

namespace tgfx {
/**
 * PlacementArray is a simple array-like container that holds a list of PlacementPtr pointers in
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
  PlacementArray(PlacementArray&& other) noexcept : _data(other._data), _size(other._size) {
    other._data = nullptr;
    other._size = 0;
  }

  /**
   * Constructs a PlacementArray from another PlacementArray of a different type.
   */
  template <typename U>
  PlacementArray(PlacementArray<U>&& other) noexcept : _data(other._data), _size(other.size) {
    static_assert(std::is_base_of_v<T, U>, "U must be derived from T!");
    other._data = nullptr;
    other._size = 0;
  }

  /**
   * Assigns a PlacementArray by moving from another PlacementArray.
   */
  PlacementArray& operator=(PlacementArray&& other) noexcept {
    if (this != &other) {
      clear();
      _data = other._data;
      _size = other._size;
      other._data = nullptr;
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
        _data[i].~PlacementPtr<T>();
      }
      _data = nullptr;
      _size = 0;
    }
  }

  /**
   * Returns a pointer to the element at the specified index.
   */
  PlacementPtr<T>& operator[](size_t index) {
    DEBUG_ASSERT(index < _size);
    return _data[index];
  }

  /**
   * Returns a pointer to the element at the specified index.
   */
  const PlacementPtr<T>& operator[](size_t index) const {
    DEBUG_ASSERT(index < _size);
    return _data[index];
  }

  /**
   * Returns a pointer to the element at the front of the PlacementArray.
   */
  PlacementPtr<T>& front() {
    DEBUG_ASSERT(_size > 0);
    return _data[0];
  }

  /**
   * Returns a pointer to the element at the front of the PlacementArray.
   */
  const PlacementPtr<T>& front() const {
    DEBUG_ASSERT(_size > 0);
    return _data[0];
  }

  /**
   * Returns a pointer to the element at the back of the PlacementArray.
   */
  PlacementPtr<T>& back() {
    DEBUG_ASSERT(_size > 0);
    return _data[_size - 1];
  }

  /**
   * Returns a pointer to the element at the back of the PlacementArray.
   */
  const PlacementPtr<T>& back() const {
    DEBUG_ASSERT(_size > 0);
    return _data[_size - 1];
  }

  /**
   * Returns a pointer to the underlying data of the PlacementArray.
   */
  PlacementPtr<T>* data() {
    return _data;
  }

  /**
   * The iterator class for the PlacementArray. It allows iterating over the elements in the array.
   */
  class Iterator {
   public:
    Iterator(PlacementPtr<T>* data, size_t index) : data(data), index(index) {
    }

    Iterator& operator++() {
      ++index;
      return *this;
    }

    bool operator!=(const Iterator& other) const {
      return index != other.index;
    }

    PlacementPtr<T>& operator*() {
      return data[index];
    }

   private:
    PlacementPtr<T>* data = nullptr;
    size_t index = 0;
  };

  /**
   * Returns an iterator to the beginning of the PlacementArray.
   */
  Iterator begin() {
    return Iterator(_data, 0);
  }

  /**
   * Returns an iterator to the end of the PlacementArray.
   */
  Iterator end() {
    return Iterator(_data, _size);
  }

  /**
   * The const iterator class for the PlacementArray. It allows iterating over the elements in the
   * array.
   */
  class ConstIterator {
   public:
    ConstIterator(const PlacementPtr<T>* data, size_t index) : data(data), index(index) {
    }

    ConstIterator& operator++() {
      ++index;
      return *this;
    }

    bool operator!=(const ConstIterator& other) const {
      return index != other.index;
    }

    const PlacementPtr<T>& operator*() const {
      return data[index];
    }

   private:
    const PlacementPtr<T>* data = nullptr;
    size_t index = 0;
  };

  /**
   * Returns a const iterator to the beginning of the PlacementArray.
   */
  ConstIterator begin() const {
    return ConstIterator(_data, 0);
  }

  /**
   * Returns a const iterator to the end of the PlacementArray.
   */
  ConstIterator end() const {
    return ConstIterator(_data, _size);
  }

 private:
  PlacementPtr<T>* _data = nullptr;
  size_t _size = 0;

  /**
   * Constructs a PlacementArray with the specified data and size.
   */
  PlacementArray(PlacementPtr<T>* data, size_t size) : _data(data), _size(size) {
  }

  friend class BlockAllocator;
};
}  // namespace tgfx

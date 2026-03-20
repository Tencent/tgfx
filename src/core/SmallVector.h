/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include <algorithm>
#include <cstddef>
#include <new>
#include <utility>

namespace tgfx {

/**
 * A vector-like container that stores a small number of elements inline (on the stack),
 * avoiding heap allocation for small sizes. When the number of elements exceeds N,
 * it falls back to heap allocation.
 *
 * @tparam T Element type.
 * @tparam N Number of elements to store inline.
 */
template <typename T, size_t N>
class SmallVector {
 public:
  SmallVector() = default;

  SmallVector(const SmallVector& other) {
    reserve(other._size);
    for (size_t i = 0; i < other._size; ++i) {
      new (data() + i) T(other[i]);
    }
    _size = other._size;
  }

  SmallVector(SmallVector&& other) noexcept {
    if (other.isUsingHeap()) {
      _heapData = other._heapData;
      _capacity = other._capacity;
      _size = other._size;
      other._heapData = nullptr;
      other._capacity = N;
      other._size = 0;
    } else {
      for (size_t i = 0; i < other._size; ++i) {
        new (data() + i) T(std::move(other[i]));
        other[i].~T();
      }
      _size = other._size;
      other._size = 0;
    }
  }

  SmallVector& operator=(const SmallVector& other) {
    if (this != &other) {
      clear();
      reserve(other._size);
      for (size_t i = 0; i < other._size; ++i) {
        new (data() + i) T(other[i]);
      }
      _size = other._size;
    }
    return *this;
  }

  SmallVector& operator=(SmallVector&& other) noexcept {
    if (this != &other) {
      clear();
      freeHeap();

      if (other.isUsingHeap()) {
        _heapData = other._heapData;
        _capacity = other._capacity;
        _size = other._size;
        other._heapData = nullptr;
        other._capacity = N;
        other._size = 0;
      } else {
        for (size_t i = 0; i < other._size; ++i) {
          new (data() + i) T(std::move(other[i]));
          other[i].~T();
        }
        _size = other._size;
        other._size = 0;
      }
    }
    return *this;
  }

  ~SmallVector() {
    clear();
    freeHeap();
  }

  T& operator[](size_t index) {
    return data()[index];
  }

  const T& operator[](size_t index) const {
    return data()[index];
  }

  T* data() {
    return isUsingHeap() ? _heapData : reinterpret_cast<T*>(_inlineStorage);
  }

  const T* data() const {
    return isUsingHeap() ? _heapData : reinterpret_cast<const T*>(_inlineStorage);
  }

  T* begin() {
    return data();
  }

  const T* begin() const {
    return data();
  }

  T* end() {
    return data() + _size;
  }

  const T* end() const {
    return data() + _size;
  }

  bool empty() const {
    return _size == 0;
  }

  size_t size() const {
    return _size;
  }

  void reserve(size_t newCapacity) {
    if (newCapacity <= _capacity) {
      return;
    }
    grow(newCapacity);
  }

  void clear() {
    for (size_t i = 0; i < _size; ++i) {
      data()[i].~T();
    }
    _size = 0;
  }

  void push_back(const T& value) {
    ensureCapacity(_size + 1);
    new (data() + _size) T(value);
    ++_size;
  }

  void push_back(T&& value) {
    ensureCapacity(_size + 1);
    new (data() + _size) T(std::move(value));
    ++_size;
  }

 private:
  bool isUsingHeap() const {
    return _heapData != nullptr;
  }

  void ensureCapacity(size_t required) {
    if (required > _capacity) {
      grow(std::max(required, _capacity * 2));
    }
  }

  void grow(size_t newCapacity) {
    T* newData = static_cast<T*>(::operator new(newCapacity * sizeof(T)));

    T* oldData = data();
    for (size_t i = 0; i < _size; ++i) {
      new (newData + i) T(std::move(oldData[i]));
      oldData[i].~T();
    }

    freeHeap();
    _heapData = newData;
    _capacity = newCapacity;
  }

  void freeHeap() {
    if (_heapData) {
      ::operator delete(_heapData);
      _heapData = nullptr;
      _capacity = N;
    }
  }

  alignas(T) char _inlineStorage[sizeof(T) * N] = {};
  T* _heapData = nullptr;
  size_t _size = 0;
  size_t _capacity = N;
};

}  // namespace tgfx

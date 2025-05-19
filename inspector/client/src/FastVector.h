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

#include <assert.h>
#include <stddef.h>
#include "Alloc.h"

namespace inspector {

template <typename T>
class FastVector {
 public:
  using iterator = T*;
  using const_iterator = const T*;

  FastVector(size_t capacity)
      : m_ptr((T*)inspectorMalloc(sizeof(T) * capacity)), m_write(m_ptr), m_end(m_ptr + capacity) {
    assert(capacity != 0);
  }

  FastVector(const FastVector&) = delete;
  FastVector(FastVector&&) = delete;

  ~FastVector() {
    inspectorFree(m_ptr);
  }

  FastVector& operator=(const FastVector&) = delete;
  FastVector& operator=(FastVector&&) = delete;

  bool empty() const {
    return m_ptr == m_write;
  }
  size_t size() const {
    return size_t(m_write - m_ptr);
  }

  T* data() {
    return m_ptr;
  }
  const T* data() const {
    return m_ptr;
  };

  T* begin() {
    return m_ptr;
  }
  const T* begin() const {
    return m_ptr;
  }
  T* end() {
    return m_write;
  }
  const T* end() const {
    return m_write;
  }

  T& front() {
    assert(!empty());
    return m_ptr[0];
  }
  const T& front() const {
    assert(!empty());
    return m_ptr[0];
  }

  T& back() {
    assert(!empty());
    return m_write[-1];
  }
  const T& back() const {
    assert(!empty());
    return m_write[-1];
  }

  T& operator[](size_t idx) {
    return m_ptr[idx];
  }
  const T& operator[](size_t idx) const {
    return m_ptr[idx];
  }

  T* push_next() {
    if (m_write == m_end) AllocMore();
    return m_write++;
  }

  T* prepare_next() {
    if (m_write == m_end) AllocMore();
    return m_write;
  }

  void commit_next() {
    m_write++;
  }

  void clear() {
    m_write = m_ptr;
  }

  void swap(FastVector& vec) {
    const auto ptr1 = m_ptr;
    const auto ptr2 = vec.m_ptr;
    const auto write1 = m_write;
    const auto write2 = vec.m_write;
    const auto end1 = m_end;
    const auto end2 = vec.m_end;

    m_ptr = ptr2;
    vec.m_ptr = ptr1;
    m_write = write2;
    vec.m_write = write1;
    m_end = end2;
    vec.m_end = end1;
  }

 private:
  void AllocMore() {
    const auto cap = size_t(m_end - m_ptr) * 2;
    const auto size = size_t(m_write - m_ptr);
    T* ptr = (T*)inspectorMalloc(sizeof(T) * cap);
    memcpy(ptr, m_ptr, size * sizeof(T));
    inspectorFastFree(m_ptr);
    m_ptr = ptr;
    m_write = m_ptr + size;
    m_end = m_ptr + cap;
  }

  T* m_ptr;
  T* m_write;
  T* m_end;
};

}  // namespace inspector

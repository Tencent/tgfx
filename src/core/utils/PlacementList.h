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
#include <utility>
#include "core/utils/Log.h"
#include "core/utils/PlacementBuffer.h"

namespace tgfx {
/**
 * A singly linked list similar to std::forward_list, but it does not own the memory allocated for
 * the nodes. So it only calls the destructor of the nodes when it goes out of scope, without
 * freeing the memory.
 */
template <typename T>
class PlacementList {
 public:
  using Node = typename PlacementNode<T>::Storage;

  explicit PlacementList() = default;
  PlacementList(const PlacementList&) = delete;
  PlacementList& operator=(const PlacementList&) = delete;

  template <typename U>
  PlacementList(PlacementNode<U>&& node)
      : head(reinterpret_cast<Node*>(node.storage)), tail(reinterpret_cast<Node*>(node.storage)),
        _size(1) {
    static_assert(std::is_base_of_v<T, U>, "U must be derived from T");
    node.storage = nullptr;
  }

  PlacementList(PlacementList&& other) noexcept
      : head(other.head), tail(other.tail), _size(other._size) {
    other.head = nullptr;
    other.tail = nullptr;
    other._size = 0;
  }

  ~PlacementList() {
    clear();
  }

  PlacementList& operator=(PlacementList&& other) noexcept {
    if (this != &other) {
      clear();
      head = other.head;
      tail = other.tail;
      _size = other._size;
      other.head = nullptr;
      other.tail = nullptr;
      other._size = 0;
    }
    return *this;
  }

  /**
   * Adds a new node to the end of the list.
   */
  template <typename U>
  void append(PlacementNode<U>&& node) {
    static_assert(std::is_base_of_v<T, U>, "U must be derived from T");
    DEBUG_ASSERT(node != nullptr);
    auto newNode = reinterpret_cast<Node*>(node.storage);
    node.storage = nullptr;
    if (tail == nullptr) {
      head = tail = newNode;
    } else {
      tail->next = newNode;
      tail = newNode;
    }
    _size++;
  }

  /**
   * Returns a reference to the first element in the list.
   */
  T& front() {
    DEBUG_ASSERT(head != nullptr);
    return head->data();
  }

  /**
   * Returns a reference to the last element in the list.
   */
  T& back() {
    DEBUG_ASSERT(tail != nullptr);
    return tail->data();
  }

  /**
   * Checks if the list is empty.
   *
   * @return `true` if the list is empty, `false` otherwise.
   */
  bool empty() const {
    return head == nullptr;
  }

  /**
   * Returns the number of elements in the list.
   */
  size_t size() const {
    return _size;
  }

  /**
   * Removes all elements from the list.
   */
  void clear() {
    while (head) {
      auto oldHead = head;
      oldHead->data().~T();
      head = head->next;
    }
    tail = nullptr;
    _size = 0;
  }

  /**
   * Iterator for the PlacementList.
   */
  class Iterator {
   public:
    explicit Iterator(Node* node) : node(node) {
    }

    T& operator*() {
      return node->data();
    }

    Iterator& operator++() {
      node = node->next;
      return *this;
    }

    bool operator!=(const Iterator& other) {
      return node != other.node;
    }

   private:
    Node* node;
  };

  /**
   * Returns an Iterator to the beginning of the list.
   */
  Iterator begin() {
    return Iterator(head);
  }

  /**
   * Returns an Iterator to the end of the list.
   */
  Iterator end() {
    return Iterator(nullptr);
  }

  /**
   * ConstIterator for the PlacementList.
   */
  class ConstIterator {
   public:
    explicit ConstIterator(const Node* node) : node(node) {
    }

    const T& operator*() const {
      return node->data();
    }

    ConstIterator& operator++() {
      node = node->next;
      return *this;
    }

    bool operator!=(const ConstIterator& other) const {
      return node != other.node;
    }

   private:
    const Node* node;
  };

  /**
   * Returns a ConstIterator to the beginning of the list.
   */
  ConstIterator begin() const {
    return ConstIterator(head);
  }

  /**
   * Returns a ConstIterator to the end of the list.
   */
  ConstIterator end() const {
    return ConstIterator(nullptr);
  }

 private:
  Node* head = nullptr;  ///< A pointer to the first node in the list.
  Node* tail = nullptr;  ///< A pointer to the last node in the list.
  size_t _size = 0;      ///< The number of elements in the list.
};

}  // namespace tgfx
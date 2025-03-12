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
 * A node that can be added to a PlacementList. The node's storage must be allocated in
 * pre-allocated memory. The node does not own the memory, so it only calls the destructor of the
 * object when it goes out of scope, without freeing the memory.
 */
template <typename T>
class PlacementNode {
 public:
  // Aligning the nodes to the cache line size can improve iteration performance.
  static constexpr size_t ALIGNMENT = 64;

  struct Storage {
    Storage* next;
    uint8_t memory[sizeof(T)];

    const T& data() const {
      return *reinterpret_cast<const T*>(memory);
    }

    T& data() {
      return *reinterpret_cast<T*>(memory);
    }
  };

  /**
   * Constructs a PlacementNode with the given node storage. The storage must be allocated in a
   * pre-allocated memory block.
   */
  explicit PlacementNode(Storage* storage = nullptr) : storage(storage) {
  }

  /**
   * Constructs a PlacementNode by moving the storage from another PlacementNode of a different
   * type.
   */
  template <typename U>
  PlacementNode(PlacementNode<U>&& other) noexcept
      : storage(reinterpret_cast<Storage*>(other.storage)) {
    static_assert(std::is_base_of_v<T, U>, "U must be derived from T");
    other.storage = nullptr;
  }

  /**
   * Constructs a PlacementNode with a nullptr.
   */
  PlacementNode(std::nullptr_t) : storage(nullptr) {
  }

  /**
   * Destroys the PlacementNode and calls the destructor of the object.
   */
  ~PlacementNode() {
    if (storage) {
      storage->data().~T();
    }
  }

  /**
   * Constructs a PlacementNode by moving the storage from another PlacementNode.
   */
  PlacementNode(PlacementNode&& other) noexcept : storage(other.storage) {
    other.storage = nullptr;
  }

  PlacementNode(const PlacementNode&) = delete;
  PlacementNode& operator=(const PlacementNode&) = delete;

  /**
   * Assigns a PlacementNode to the PlacementNode by moving the storage from the other PlacementNode.
   */
  PlacementNode& operator=(PlacementNode&& other) noexcept {
    if (this != &other) {
      if (storage) {
        storage->data().~T();
      }
      storage = other.storage;
      other.storage = nullptr;
    }
    return *this;
  }

  /**
   * Assigns a PlacementNode of a different type to the PlacementNode by moving the storage from the
   * other PlacementNode.
   */
  template <typename U>
  PlacementNode& operator=(PlacementNode<U>&& other) noexcept {
    static_assert(std::is_base_of_v<T, U>, "U must be derived from T");
    auto otherStorage = reinterpret_cast<Storage*>(other.storage);
    if (storage != otherStorage) {
      if (storage) {
        storage->data().~T();
      }
      storage = otherStorage;
      other.storage = nullptr;
    }
    return *this;
  }

  /**
   * Assigns a nullptr to the PlacementNode.
   */
  PlacementNode& operator=(std::nullptr_t) noexcept {
    if (storage) {
      storage->data().~T();
    }
    storage = nullptr;
    return *this;
  }

  /**
   * Returns true if the PlacementNode is equal to another PlacementNode.
   */
  bool operator==(const PlacementNode& other) const {
    return storage == other.storage;
  }

  /**
   * Returns true if the PlacementNode is not equal to another PlacementNode.
   */
  bool operator!=(const PlacementNode& other) const {
    return storage != other.storage;
  }

  /**
   * Returns true if the PlacementNode is not equal to a nullptr.
   */
  explicit operator bool() const {
    return storage != nullptr;
  }

  T* get() const {
    return reinterpret_cast<T*>(storage);
  }

  T& operator*() const {
    return *reinterpret_cast<T*>(storage->memory);
  }

  T* operator->() const {
    return reinterpret_cast<T*>(storage->memory);
  }

 private:
  Storage* storage = nullptr;

  template <typename U>
  friend class PlacementList;

  template <typename U>
  friend class PlacementNode;
};
}  // namespace tgfx

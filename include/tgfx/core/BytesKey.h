/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace tgfx {
/**
 * A key used for hashing a byte stream.
 */
class BytesKey {
 public:
  BytesKey() = default;

  /**
   * Creates a key with the given capacity of uint32_t values.
   */
  explicit BytesKey(size_t capacity) {
    values.reserve(capacity);
  }

  /**
   * Reserves the capacity of uint32_t values.
   */
  void reserve(size_t capacity) {
    values.reserve(capacity);
  }

  /**
   * Returns true if this key is valid.
   */
  bool isValid() const {
    return !values.empty();
  }

  /**
   * Writes an uint32 value into the key.
   */
  void write(uint32_t value);

  /**
   * Writes an int value into the key.
   */
  void write(int value);

  /**
   * Writes a pointer value into the key.
   */
  void write(const void* value);

  /**
   * Writes an uint32 value into the key.
   */
  void write(const uint8_t value[4]);

  /**
   * Writes a float value into the key.
   */
  void write(float value);

  /**
   * Pointer to the key data
   */
  const uint32_t* data() const {
    return values.data();
  }

  /**
   * Returns the number of uint32_t values in the key.
   */
  size_t size() const {
    return values.size();
  }

  friend bool operator==(const BytesKey& a, const BytesKey& b) {
    return a.values == b.values;
  }

  bool operator<(const BytesKey& key) const {
    return values < key.values;
  }

 private:
  std::vector<uint32_t> values = {};

  friend struct BytesKeyHasher;
};

/**
 * The hasher for BytesKey.
 */
struct BytesKeyHasher {
  size_t operator()(const BytesKey& key) const;
};

template <typename T>
using BytesKeyMap = std::unordered_map<BytesKey, T, BytesKeyHasher>;
}  // namespace tgfx

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
#include <cassert>
#include <functional>
#include <unordered_map>
#include <vector>
#include "InspectorEvent.h"

namespace inspector {

static uint32_t HashRange(const char* input) {
  auto len = strlen(input);
  auto hash = static_cast<uint32_t>(len);
  for (size_t i = 0; i < len; ++i) {
    hash ^= static_cast<uint32_t>(input[i]) + 0x9e3779b9 + (hash << 6u) + (hash >> 2u);
  }
  return hash;
}

struct Hasher {
  size_t operator()(const char* key) const {
    return HashRange(key);
  }
};

struct Comparator {
  bool operator()(const char* lhs, const char* rhs) const {
    return strcmp(lhs, rhs) == 0;
  }
};

template <typename T>
class StringDiscovery {
 public:
  std::vector<T>& Data() {
    return m_data;
  }
  const std::vector<T>& Data() const {
    return m_data;
  }

  bool IsPending() const {
    return !m_pending.empty();
  }

  // Merge( destination, postponed )
  template <typename U>
  void StringDiscovered(uint64_t name, const StringLocation& sl, U& stringMap,
                        std::function<void(T, T)> Merge) {
    auto pit = m_pending.find(name);
    assert(pit != m_pending.end());

    auto it = m_rev.find(sl.ptr);
    if (it == m_rev.end()) {
      m_map.emplace(name, pit->second);
      m_rev.emplace(sl.ptr, pit->second);
      m_data.push_back(pit->second);
      stringMap.emplace(name, sl.ptr);
    } else {
      auto item = it->second;
      m_map.emplace(name, item);
      Merge(item, pit->second);
    }

    m_pending.erase(pit);
  }

  T Retrieve(uint64_t name, const std::function<T(uint64_t)>& Create,
             const std::function<void(uint64_t)>& Query) {
    auto it = m_map.find(name);
    if (it == m_map.end()) {
      auto pit = m_pending.find(name);
      if (pit == m_pending.end()) {
        T item = Create(name);
        if (item) {
          m_pending.emplace(name, item);
          Query(name);
        }
        return item;
      } else {
        return pit->second;
      }
    } else {
      return it->second;
    }
  }

  void AddExternal(const T& val) {
    m_data.push_back(val);
  }

 private:
  std::vector<T> m_data;
  std::unordered_map<uint64_t, T> m_pending;
  std::unordered_map<uint64_t, T> m_map;
  std::unordered_map<const char*, T, Hasher, Comparator> m_rev;
};

}  // namespace inspector
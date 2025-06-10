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
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

template <typename Key, typename T>
class WeakMap {
 public:
  std::shared_ptr<T> find(const Key& key, size_t cleanThreshold = 50) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = cacheMap_.find(key);
    if (it != cacheMap_.end()) {
      auto cachedPtr = it->second.lock();
      if (cachedPtr) {
        return cachedPtr;
      }
      cacheMap_.erase(it);
      if (cacheMap_.size() > cleanThreshold) {
        std::vector<Key> expiredKeys;
        for (const auto& item : cacheMap_) {
          if (item.second.expired()) {
            expiredKeys.push_back(item.first);
          }
        }
        for (const auto& k : expiredKeys) {
          cacheMap_.erase(k);
        }
      }
    }
    return nullptr;
  }

  void insert(const Key& key, const std::shared_ptr<T>& ptr) {
    std::lock_guard<std::mutex> lock(mutex_);
    cacheMap_[key] = std::weak_ptr<T>(ptr);
  }

 private:
  std::unordered_map<Key, std::weak_ptr<T>> cacheMap_;
  std::mutex mutex_;
};

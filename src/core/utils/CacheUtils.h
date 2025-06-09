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
#include <string>
#include <unordered_map>
#include <vector>

template <typename T>
std::shared_ptr<T> FindAndCleanCache(std::unordered_map<std::string, std::weak_ptr<T>>& cacheMap,
                                     const std::string& key, size_t cleanThreshold = 50) {
  auto it = cacheMap.find(key);
  if (it != cacheMap.end()) {
    auto& weak = it->second;
    auto cachedPtr = weak.lock();
    if (cachedPtr) {
      return cachedPtr;
    }
    cacheMap.erase(it);

    if (cacheMap.size() > cleanThreshold) {
      std::vector<std::string> needRemoveList;
      for (const auto& item : cacheMap) {
        if (item.second.expired()) {
          needRemoveList.push_back(item.first);
        }
      }
      for (const auto& k : needRemoveList) {
        cacheMap.erase(k);
      }
    }
  }
  return nullptr;
}

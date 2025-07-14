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

#include "ResourceHandle.h"

namespace tgfx {
ResourceHandle::ResourceHandle(const UniqueKey& key) : uniqueKey(key) {
  uniqueKey.addStrong();
}

ResourceHandle::ResourceHandle(UniqueKey&& key) noexcept : uniqueKey(std::move(key)) {
  uniqueKey.addStrong();
}

ResourceHandle::ResourceHandle(const ResourceHandle& handle) : uniqueKey(handle.uniqueKey) {
  uniqueKey.addStrong();
}

ResourceHandle::ResourceHandle(ResourceHandle&& handle) noexcept
    : uniqueKey(std::move(handle.uniqueKey)) {
}

ResourceHandle::~ResourceHandle() {
  uniqueKey.releaseStrong();
}

ResourceHandle& ResourceHandle::operator=(const UniqueKey& key) {
  uniqueKey.releaseStrong();
  uniqueKey = key;
  uniqueKey.addStrong();
  return *this;
}

ResourceHandle& ResourceHandle::operator=(UniqueKey&& key) noexcept {
  uniqueKey.releaseStrong();
  uniqueKey = std::move(key);
  uniqueKey.addStrong();
  return *this;
}

ResourceHandle& ResourceHandle::operator=(const ResourceHandle& handle) {
  if (this == &handle) {
    return *this;
  }
  uniqueKey.releaseStrong();
  uniqueKey = handle.uniqueKey;
  uniqueKey.addStrong();
  return *this;
}

ResourceHandle& ResourceHandle::operator=(ResourceHandle&& handle) noexcept {
  if (this == &handle) {
    return *this;
  }
  uniqueKey.releaseStrong();
  uniqueKey = std::move(handle.uniqueKey);
  return *this;
}

bool ResourceHandle::operator==(const ResourceHandle& handle) const {
  return uniqueKey == handle.uniqueKey;
}

bool ResourceHandle::operator!=(const ResourceHandle& handle) const {
  return uniqueKey != handle.uniqueKey;
}
}  // namespace tgfx

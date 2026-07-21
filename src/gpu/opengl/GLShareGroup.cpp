/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "gpu/opengl/GLShareGroup.h"
#include <algorithm>
#include <unordered_map>

namespace tgfx {
static std::mutex shareGroupMapLocker = {};
static std::unordered_map<void*, std::weak_ptr<GLShareGroup>> shareGroupMap = {};

GLShareGroup::GLShareGroup() : _locker(std::make_shared<std::mutex>()) {
}

GLShareGroup::~GLShareGroup() {
  std::lock_guard<std::mutex> autoLock(shareGroupMapLocker);
  for (auto handle : members) {
    auto it = shareGroupMap.find(handle);
    if (it != shareGroupMap.end() && it->second.expired()) {
      shareGroupMap.erase(it);
    }
  }
}

std::shared_ptr<GLShareGroup> GLShareGroup::Find(void* nativeHandle) {
  if (nativeHandle == nullptr) {
    return nullptr;
  }
  std::lock_guard<std::mutex> autoLock(shareGroupMapLocker);
  auto it = shareGroupMap.find(nativeHandle);
  if (it == shareGroupMap.end()) {
    return nullptr;
  }
  auto group = it->second.lock();
  if (group == nullptr) {
    shareGroupMap.erase(it);
  }
  return group;
}

std::shared_ptr<GLShareGroup> GLShareGroup::GetOrCreate(void* sharedHandle) {
  if (sharedHandle == nullptr) {
    return std::shared_ptr<GLShareGroup>(new GLShareGroup());
  }
  std::lock_guard<std::mutex> autoLock(shareGroupMapLocker);
  auto it = shareGroupMap.find(sharedHandle);
  if (it != shareGroupMap.end()) {
    auto group = it->second.lock();
    if (group != nullptr) {
      return group;
    }
    shareGroupMap.erase(it);
  }
  auto group = std::shared_ptr<GLShareGroup>(new GLShareGroup());
  group->members.push_back(sharedHandle);
  shareGroupMap[sharedHandle] = group;
  return group;
}

void GLShareGroup::addMember(void* nativeHandle) {
  if (nativeHandle == nullptr) {
    return;
  }
  std::lock_guard<std::mutex> autoLock(shareGroupMapLocker);
  if (std::find(members.begin(), members.end(), nativeHandle) == members.end()) {
    members.push_back(nativeHandle);
  }
  shareGroupMap[nativeHandle] = shared_from_this();
}

void GLShareGroup::removeMember(void* nativeHandle) {
  if (nativeHandle == nullptr) {
    return;
  }
  std::lock_guard<std::mutex> autoLock(shareGroupMapLocker);
  auto it = std::find(members.begin(), members.end(), nativeHandle);
  if (it != members.end()) {
    members.erase(it);
  }
  auto mapIt = shareGroupMap.find(nativeHandle);
  if (mapIt != shareGroupMap.end()) {
    shareGroupMap.erase(mapIt);
  }
}
}  // namespace tgfx

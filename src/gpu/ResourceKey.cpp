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

#include "gpu/ResourceKey.h"
#include "gpu/UniqueDomain.h"
#include "utils/Log.h"

namespace tgfx {
ResourceKey ResourceKey::NewWeak() {
  return {new UniqueDomain(), false};
}

ResourceKey ResourceKey::NewStrong() {
  return {new UniqueDomain(), true};
}

ResourceKey::ResourceKey(UniqueDomain* block, bool strong) : uniqueDomain(block), strong(strong) {
  DEBUG_ASSERT(uniqueDomain != nullptr);
  uniqueDomain->addReference(strong);
}

ResourceKey::ResourceKey(const ResourceKey& key)
    : uniqueDomain(key.uniqueDomain), strong(key.strong) {
  if (uniqueDomain != nullptr) {
    uniqueDomain->addReference(strong);
  }
}

ResourceKey::ResourceKey(ResourceKey&& key) noexcept
    : uniqueDomain(key.uniqueDomain), strong(key.strong) {
  key.uniqueDomain = nullptr;
}

ResourceKey::~ResourceKey() {
  if (uniqueDomain != nullptr) {
    uniqueDomain->releaseReference(strong);
  }
}

uint64_t ResourceKey::domain() const {
  return uniqueDomain != nullptr ? uniqueDomain->uniqueID() : 0;
}

ResourceKey ResourceKey::makeStrong() const {
  return {uniqueDomain, true};
}

ResourceKey ResourceKey::makeWeak() const {
  return {uniqueDomain, false};
}

long ResourceKey::useCount() const {
  return uniqueDomain != nullptr ? uniqueDomain->useCount() : 0;
}

long ResourceKey::strongCount() const {
  return uniqueDomain != nullptr ? uniqueDomain->strongCount() : 0;
}

ResourceKey& ResourceKey::operator=(const ResourceKey& key) {
  if (this == &key) {
    return *this;
  }
  if (uniqueDomain != nullptr) {
    uniqueDomain->releaseReference(strong);
  }
  uniqueDomain = key.uniqueDomain;
  strong = key.strong;
  if (uniqueDomain != nullptr) {
    uniqueDomain->addReference(strong);
  }
  return *this;
}

ResourceKey& ResourceKey::operator=(ResourceKey&& key) noexcept {
  if (this == &key) {
    return *this;
  }
  if (uniqueDomain != nullptr) {
    uniqueDomain->releaseReference(strong);
  }
  uniqueDomain = key.uniqueDomain;
  strong = key.strong;
  key.uniqueDomain = nullptr;
  return *this;
}
}  // namespace tgfx
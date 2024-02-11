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
ResourceKey ResourceKey::Make() {
  return ResourceKey(new UniqueDomain());
}

ResourceKey::ResourceKey(UniqueDomain* block) : uniqueDomain(block) {
  DEBUG_ASSERT(uniqueDomain != nullptr);
  uniqueDomain->addReference();
}

ResourceKey::ResourceKey(const ResourceKey& key) : uniqueDomain(key.uniqueDomain) {
  if (uniqueDomain != nullptr) {
    uniqueDomain->addReference();
  }
}

ResourceKey::ResourceKey(ResourceKey&& key) noexcept : uniqueDomain(key.uniqueDomain) {
  key.uniqueDomain = nullptr;
}

ResourceKey::~ResourceKey() {
  if (uniqueDomain != nullptr) {
    uniqueDomain->releaseReference();
  }
}

uint32_t ResourceKey::domain() const {
  return uniqueDomain != nullptr ? uniqueDomain->uniqueID() : 0;
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
    uniqueDomain->releaseReference();
  }
  uniqueDomain = key.uniqueDomain;
  if (uniqueDomain != nullptr) {
    uniqueDomain->addReference();
  }
  return *this;
}

ResourceKey& ResourceKey::operator=(ResourceKey&& key) noexcept {
  if (this == &key) {
    return *this;
  }
  if (uniqueDomain != nullptr) {
    uniqueDomain->releaseReference();
  }
  uniqueDomain = key.uniqueDomain;
  key.uniqueDomain = nullptr;
  return *this;
}

bool ResourceKey::operator==(const ResourceKey& key) const {
  return uniqueDomain == key.uniqueDomain;
}

bool ResourceKey::operator!=(const ResourceKey& key) const {
  return uniqueDomain != key.uniqueDomain;
}

void ResourceKey::addStrong() {
  if (uniqueDomain != nullptr) {
    uniqueDomain->addStrong();
  }
}

void ResourceKey::releaseStrong() {
  if (uniqueDomain != nullptr) {
    uniqueDomain->releaseStrong();
  }
}
}  // namespace tgfx
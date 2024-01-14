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
UniqueKey UniqueKey::MakeWeak() {
  return {new UniqueDomain(), false};
}

UniqueKey UniqueKey::MakeStrong() {
  return {new UniqueDomain(), true};
}

UniqueKey::UniqueKey(UniqueDomain* block, bool strong) : uniqueDomain(block), strong(strong) {
  DEBUG_ASSERT(uniqueDomain != nullptr);
  uniqueDomain->addReference(strong);
}

UniqueKey::UniqueKey(const UniqueKey& key) : uniqueDomain(key.uniqueDomain), strong(key.strong) {
  if (uniqueDomain != nullptr) {
    uniqueDomain->addReference(strong);
  }
}

UniqueKey::UniqueKey(UniqueKey&& key) noexcept
    : uniqueDomain(key.uniqueDomain), strong(key.strong) {
  key.uniqueDomain = nullptr;
}

UniqueKey::~UniqueKey() {
  if (uniqueDomain != nullptr) {
    uniqueDomain->releaseReference(strong);
  }
}

uint32_t UniqueKey::domainID() const {
  return uniqueDomain != nullptr ? uniqueDomain->uniqueID() : 0;
}

UniqueKey UniqueKey::makeStrong() const {
  return {uniqueDomain, true};
}

UniqueKey UniqueKey::makeWeak() const {
  return {uniqueDomain, false};
}

long UniqueKey::useCount() const {
  return uniqueDomain != nullptr ? uniqueDomain->useCount() : 0;
}

long UniqueKey::strongCount() const {
  return uniqueDomain != nullptr ? uniqueDomain->strongCount() : 0;
}

UniqueKey& UniqueKey::operator=(const UniqueKey& key) {
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

UniqueKey& UniqueKey::operator=(UniqueKey&& key) noexcept {
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
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
#include "utils/HashRange.h"
#include "utils/Log.h"

namespace tgfx {
ResourceKey::ResourceKey(uint32_t* data, size_t count) : data(data), count(count) {
  DEBUG_ASSERT(data == nullptr || count >= 1);
}

ResourceKey::~ResourceKey() {
  delete[] data;
}

ResourceKey::ResourceKey(const ResourceKey& that) {
  copy(that);
}

bool ResourceKey::equal(const ResourceKey& that) const {
  if (count != that.count) {
    return false;
  }
  return memcmp(data, that.data, count * sizeof(uint32_t)) == 0;
}

void ResourceKey::copy(const ResourceKey& that) {
  if (data == that.data) {
    return;
  }
  copy(that.data, that.count);
}

void ResourceKey::copy(const uint32_t* newData, size_t newCount, size_t offset) {
  delete[] data;
  count = newCount + offset;
  if (count > 0) {
    data = new (std::nothrow) uint32_t[count];
    if (data == nullptr) {
      count = 0;
      LOGE("Failed to allocate the data of ResourceKey!");
      return;
    }
    memcpy(data + offset, newData, newCount * sizeof(uint32_t));
  } else {
    data = nullptr;
  }
}

ScratchKey::ScratchKey(uint32_t* data, size_t count) : ResourceKey(data, count) {
}

ScratchKey& ScratchKey::operator=(const BytesKey& that) {
  copy(that.data(), that.size(), 1);
  if (data != nullptr) {
    data[0] = HashRange(that.data(), that.size());
  }
  return *this;
}

UniqueKey UniqueKey::Next() {
  return UniqueKey(new UniqueDomain());
}

UniqueKey::UniqueKey(UniqueDomain* block) : uniqueDomain(block) {
  DEBUG_ASSERT(uniqueDomain != nullptr);
}

UniqueKey::UniqueKey(const UniqueKey& key) : uniqueDomain(key.uniqueDomain) {
  if (uniqueDomain != nullptr) {
    uniqueDomain->addReference();
  }
}

UniqueKey::UniqueKey(UniqueKey&& key) noexcept : uniqueDomain(key.uniqueDomain) {
  key.uniqueDomain = nullptr;
}

UniqueKey::~UniqueKey() {
  if (uniqueDomain != nullptr) {
    uniqueDomain->releaseReference();
  }
}

uint32_t UniqueKey::domain() const {
  return uniqueDomain != nullptr ? uniqueDomain->uniqueID() : 0;
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
    uniqueDomain->releaseReference();
  }
  uniqueDomain = key.uniqueDomain;
  if (uniqueDomain != nullptr) {
    uniqueDomain->addReference();
  }
  return *this;
}

UniqueKey& UniqueKey::operator=(UniqueKey&& key) noexcept {
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

bool UniqueKey::operator==(const UniqueKey& key) const {
  return uniqueDomain == key.uniqueDomain;
}

bool UniqueKey::operator!=(const UniqueKey& key) const {
  return uniqueDomain != key.uniqueDomain;
}

void UniqueKey::addStrong() {
  if (uniqueDomain != nullptr) {
    uniqueDomain->addStrong();
  }
}

void UniqueKey::releaseStrong() {
  if (uniqueDomain != nullptr) {
    uniqueDomain->releaseStrong();
  }
}

LazyUniqueKey::~LazyUniqueKey() {
  reset();
}

UniqueKey LazyUniqueKey::get() {
  auto domain = uniqueDomain.load(std::memory_order_acquire);
  if (domain == nullptr) {
    auto newDomain = new UniqueDomain();
    if (uniqueDomain.compare_exchange_strong(domain, newDomain, std::memory_order_acq_rel)) {
      domain = newDomain;
    } else {
      newDomain->releaseReference();
    }
  }
  domain->addReference();
  return UniqueKey(domain);
}

void LazyUniqueKey::reset() {
  auto oldDomain = uniqueDomain.exchange(nullptr, std::memory_order_acq_rel);
  if (oldDomain != nullptr) {
    oldDomain->releaseReference();
  }
}
}  // namespace tgfx
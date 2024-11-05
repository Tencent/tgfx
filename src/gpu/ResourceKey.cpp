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
#include "core/utils/HashRange.h"
#include "core/utils/Log.h"
#include "gpu/UniqueDomain.h"

namespace tgfx {
static uint32_t* CopyData(const uint32_t* data, size_t count, size_t offset = 0) {
  if (data == nullptr || count == 0) {
    return nullptr;
  }
  auto newData = new (std::nothrow) uint32_t[count + offset];
  if (newData == nullptr) {
    LOGE("Failed to allocate the data of ResourceKey!");
    return nullptr;
  }
  memcpy(newData + offset, data, count * sizeof(uint32_t));
  return newData;
}

ResourceKey::ResourceKey(uint32_t* data, size_t count) : data(data), count(count) {
  DEBUG_ASSERT(data == nullptr || count >= 1);
}

ResourceKey::~ResourceKey() {
  delete[] data;
}

ResourceKey::ResourceKey(const ResourceKey& that)
    : data(CopyData(that.data, that.count)), count(that.count) {
}

ResourceKey::ResourceKey(ResourceKey&& key) noexcept : data(key.data), count(key.count) {
  key.data = nullptr;
  key.count = 0;
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
  data = CopyData(that.data, that.count);
  count = that.count;
}

ScratchKey::ScratchKey(uint32_t* data, size_t count) : ResourceKey(data, count) {
}

ScratchKey& ScratchKey::operator=(const BytesKey& that) {
  data = CopyData(that.data(), that.size(), 1);
  if (data != nullptr) {
    data[0] = HashRange(that.data(), that.size());
    count = that.size() + 1;
  } else {
    count = 0;
  }
  return *this;
}

UniqueKey UniqueKey::Make() {
  return UniqueKey(new UniqueDomain());
}

UniqueKey UniqueKey::Combine(const UniqueKey& uniqueKey, const BytesKey& bytesKey) {
  if (uniqueKey.empty()) {
    return {};
  }
  if (bytesKey.size() == 0) {
    return uniqueKey;
  }
  auto offset = std::max(uniqueKey.count, static_cast<size_t>(2));
  auto data = CopyData(bytesKey.data(), bytesKey.size(), offset);
  if (data == nullptr) {
    return uniqueKey;
  }
  if (uniqueKey.count > 2) {
    memcpy(data + 2, uniqueKey.data + 2, uniqueKey.count - 2);
  }
  auto count = bytesKey.size() + offset;
  auto domain = uniqueKey.uniqueDomain;
  data[1] = domain->uniqueID();
  data[0] = HashRange(data + 1, count - 1);
  domain->addReference();
  return {data, count, domain};
}

static uint32_t* MakeDomainData(UniqueDomain* uniqueDomain) {
  DEBUG_ASSERT(uniqueDomain != nullptr);
  return new uint32_t[1]{uniqueDomain->uniqueID()};
}

UniqueKey::UniqueKey(UniqueDomain* domain)
    : ResourceKey(MakeDomainData(domain), 1), uniqueDomain(domain) {
}

UniqueKey::UniqueKey(uint32_t* data, size_t count, UniqueDomain* domain)
    : ResourceKey(data, count), uniqueDomain(domain) {
}

UniqueKey::UniqueKey(const UniqueKey& key) : ResourceKey(key), uniqueDomain(key.uniqueDomain) {
  if (uniqueDomain != nullptr) {
    uniqueDomain->addReference();
  }
}

UniqueKey::UniqueKey(UniqueKey&& key) noexcept
    : ResourceKey(key.data, key.count), uniqueDomain(key.uniqueDomain) {
  key.data = nullptr;
  key.count = 0;
  key.uniqueDomain = nullptr;
}

UniqueKey::UniqueKey(const UniqueType& type)
    : ResourceKey(MakeDomainData(type.domain), 1), uniqueDomain(type.domain) {
  if (uniqueDomain != nullptr) {
    uniqueDomain->addReference();
  }
}

UniqueKey::UniqueKey(tgfx::UniqueType&& type) noexcept
    : ResourceKey(MakeDomainData(type.domain), 1), uniqueDomain(type.domain) {
  type.domain = nullptr;
}

UniqueKey::~UniqueKey() {
  if (uniqueDomain != nullptr) {
    uniqueDomain->releaseReference();
  }
}

uint32_t UniqueKey::domainID() const {
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
  copy(key);
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
  delete[] data;
  data = key.data;
  count = key.count;
  key.uniqueDomain = nullptr;
  key.data = nullptr;
  key.count = 0;
  return *this;
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

UniqueKey LazyUniqueKey::get() const {
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
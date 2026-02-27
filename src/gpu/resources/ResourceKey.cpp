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

#include "ResourceKey.h"
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

ResourceKey& ResourceKey::operator=(const ResourceKey& that) {
  if (this == &that) {
    return *this;
  }
  delete[] data;
  data = CopyData(that.data, that.count);
  count = that.count;
  return *this;
}

bool ResourceKey::operator==(const ResourceKey& that) const {
  if (count != that.count) {
    return false;
  }
  return memcmp(data, that.data, count * sizeof(uint32_t)) == 0;
}

ScratchKey::ScratchKey(uint32_t* data, size_t count) : ResourceKey(data, count) {
}

ScratchKey& ScratchKey::operator=(const BytesKey& that) {
  delete[] data;
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

UniqueKey UniqueKey::Append(const UniqueKey& uniqueKey, const uint32_t* data, size_t count) {
  if (uniqueKey.empty()) {
    return {};
  }
  if (count == 0) {
    return uniqueKey;
  }
  auto offset = std::max(uniqueKey.count, static_cast<size_t>(2));
  auto newData = CopyData(data, count, offset);
  if (newData == nullptr) {
    return uniqueKey;
  }
  if (uniqueKey.count > 2) {
    memcpy(newData + 2, uniqueKey.data + 2, (uniqueKey.count - 2) * sizeof(uint32_t));
  }
  auto newCount = count + offset;
  auto domain = uniqueKey.uniqueDomain;
  newData[1] = domain->uniqueID();
  newData[0] = HashRange(newData + 1, newCount - 1);
  domain->addReference();
  return {newData, newCount, domain};
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
  ResourceKey::operator=(key);
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

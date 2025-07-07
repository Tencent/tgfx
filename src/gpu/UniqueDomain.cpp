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

#include "UniqueDomain.h"

namespace tgfx {
static constexpr uint32_t InvalidDomain = 0;

static uint32_t NextDomainID() {
  static std::atomic<uint32_t> nextID{1};
  uint32_t id;
  do {
    id = nextID.fetch_add(1, std::memory_order_relaxed);
  } while (id == InvalidDomain);
  return id;
}

UniqueDomain::UniqueDomain() : _uniqueID(NextDomainID()) {
}

long UniqueDomain::useCount() const {
  return _useCount.load(std::memory_order_relaxed);
}

long UniqueDomain::strongCount() const {
  return _strongCount.load(std::memory_order_relaxed);
}

void UniqueDomain::addReference() {
  _useCount.fetch_add(1, std::memory_order_relaxed);
}

void UniqueDomain::releaseReference() {
  if (_useCount.fetch_add(-1, std::memory_order_acq_rel) <= 1) {
    delete this;
  }
}

void UniqueDomain::addStrong() {
  _strongCount.fetch_add(1, std::memory_order_relaxed);
}

void UniqueDomain::releaseStrong() {
  _strongCount.fetch_add(-1, std::memory_order_acq_rel);
}
}  // namespace tgfx

/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

#include "tgfx/gpu/UniqueType.h"
#include "gpu/UniqueDomain.h"

namespace tgfx {

UniqueType UniqueType::Next() {
  return UniqueType(new UniqueDomain());
}

UniqueType::UniqueType(UniqueDomain* domain) : domain(domain) {
}

UniqueType::UniqueType(const UniqueType& type) : domain(type.domain) {
  if (domain != nullptr) {
    domain->addReference();
  }
}

UniqueType::UniqueType(UniqueType&& type) noexcept : domain(type.domain) {
  type.domain = nullptr;
}

UniqueType::~UniqueType() {
  if (domain != nullptr) {
    domain->releaseReference();
  }
}

uint32_t UniqueType::uniqueID() const {
  return domain != nullptr ? domain->uniqueID() : 0;
}

UniqueType& UniqueType::operator=(const UniqueType& type) {
  if (this == &type) {
    return *this;
  }
  if (domain != nullptr) {
    domain->releaseReference();
  }
  domain = type.domain;
  if (domain != nullptr) {
    domain->addReference();
  }
  return *this;
}

UniqueType& UniqueType::operator=(UniqueType&& type) noexcept {
  if (this == &type) {
    return *this;
  }
  if (domain != nullptr) {
    domain->releaseReference();
  }
  domain = type.domain;
  type.domain = nullptr;
  return *this;
}

void UniqueType::addStrong() {
  if (domain != nullptr) {
    domain->addStrong();
  }
}

void UniqueType::releaseStrong() {
  if (domain != nullptr) {
    domain->releaseStrong();
  }
}
}  // namespace tgfx

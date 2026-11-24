/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License") you may not use this file except
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

#include "gpu/ir/ValueRef.h"

namespace tgfx {

ValueID ValueTable::Intern(const std::string& name, SLType type, ValueOrigin origin) {
  if (name.empty()) {
    return INVALID_VALUE_ID;
  }
  auto existing = byName.find(name);
  if (existing != byName.end()) {
    auto& value = values[existing->second];
    if (type != SLType::Void) {
      if (value.type != SLType::Void && value.type != type) {
        return INVALID_VALUE_ID;
      }
      value.type = type;
    }
    if (value.origin == ValueOrigin::Literal && origin != ValueOrigin::Literal) {
      value.origin = origin;
    }
    return value.id;
  }
  if (values.size() >= static_cast<size_t>(INVALID_VALUE_ID)) {
    return INVALID_VALUE_ID;
  }
  ValueRef value = {};
  value.id = static_cast<ValueID>(values.size());
  value.type = type;
  value.origin = origin;
  value.name = name;
  byName.emplace(name, value.id);
  values.push_back(std::move(value));
  return values.back().id;
}

const ValueRef* ValueTable::Find(const std::string& name) const {
  auto found = byName.find(name);
  if (found == byName.end()) {
    return nullptr;
  }
  return &values[found->second];
}

const ValueRef* ValueTable::Find(ValueID id) const {
  if (id == INVALID_VALUE_ID || id >= values.size()) {
    return nullptr;
  }
  return &values[id];
}

}  // namespace tgfx

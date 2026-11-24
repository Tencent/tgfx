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

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include "gpu/SLType.h"

namespace tgfx {

using ValueID = uint32_t;
constexpr ValueID INVALID_VALUE_ID = static_cast<ValueID>(-1);

enum class ValueOrigin : uint8_t {
  Literal,
  Attribute,
  Uniform,
  Sampler,
  Varying,
  Local,
  CallResult,
};

struct ValueRef {
  ValueID id = INVALID_VALUE_ID;
  SLType type = SLType::Void;
  ValueOrigin origin = ValueOrigin::Literal;
  std::string name = {};

  ValueRef() = default;

  ValueRef(const std::string& valueName) : name(valueName) {
  }

  ValueRef(const char* valueName) : name(valueName != nullptr ? valueName : "") {
  }

  ValueRef(const std::string& valueName, SLType valueType,
           ValueOrigin valueOrigin = ValueOrigin::Literal)
      : type(valueType), origin(valueOrigin), name(valueName) {
  }
};

/**
 * The program's value symbol table. Names remain the printing spelling, while the ID is the stable
 * handle validation and later backends use. Intern is monotonic: a name keeps its ID, and a later,
 * more specific type fills in a previously unknown one. A genuine type conflict returns
 * INVALID_VALUE_ID rather than silently choosing one.
 */
class ValueTable {
 public:
  ValueID Intern(const std::string& name, SLType type, ValueOrigin origin);

  const ValueRef* Find(const std::string& name) const;

  const ValueRef* Find(ValueID id) const;

  size_t size() const {
    return values.size();
  }

 private:
  std::vector<ValueRef> values = {};
  std::unordered_map<std::string, ValueID> byName = {};
};

}  // namespace tgfx

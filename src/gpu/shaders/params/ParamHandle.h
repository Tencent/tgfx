/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once
#include <cstdint>
#include <initializer_list>

namespace tgfx {

// The value kinds a module parameter can carry. The kind pins the writer overload (a wrong value
// type is a compile error at the Bind call) and the byte size the plan resolves against the
// reflected uniform block.
enum class ParamType : uint8_t {
  Float,
  Float2,
  Float3,
  Float4,
  Int,
  Float3x3,     // uploaded through the 48-byte padded layout UniformData::setData(Matrix) uses
  Float4Array,  // one element per chain slot
  IntArray,     // one element per chain slot
};

constexpr size_t ParamElementSize(ParamType type) {
  switch (type) {
    case ParamType::Float:
      return 4;
    case ParamType::Float2:
      return 8;
    case ParamType::Float3:
      return 12;
    case ParamType::Float4:
    case ParamType::Float4Array:
      return 16;
    case ParamType::Int:
    case ParamType::IntArray:
      return 4;
    case ParamType::Float3x3:
      return 48;
  }
  return 0;
}

// A typed reference to one field of a module's parameters: the field's name in the reflected
// uniform block and its index within the module. Declared as constexpr members of the module, so
// the name, the index and the value type live in one place and a mismatched writer call is a
// compile error. Indices must be dense from zero in the module's FieldDecls order.
template <ParamType Type>
struct Param {
  const char* name;
  uint32_t index;
};

// The type-erased view of a Param the plan resolves against the reflection.
struct ParamDesc {
  const char* name;
  ParamType type;
  uint32_t index;
};

template <ParamType Type>
constexpr ParamDesc ParamDescOf(Param<Type> param) {
  return {param.name, Type, param.index};
}

// The subset of a module's fields one instance writes. A module serving several pipelines
// declares the masks it needs: a fill kernel's instance writes every field, a chain leaf writes
// the per-slot subset, an absent local mask writes none.
class FieldMask {
 public:
  constexpr FieldMask() = default;

  constexpr bool test(uint32_t index) const {
    return index < 64 && ((bits >> index) & 1) != 0;
  }

  constexpr bool any() const {
    return bits != 0;
  }

  // Every field of a module declaring fieldCount fields.
  static constexpr FieldMask All(uint32_t fieldCount) {
    return fieldCount >= 64 ? FieldMask(~static_cast<uint64_t>(0))
                            : FieldMask((1ull << fieldCount) - 1);
  }

  static constexpr FieldMask None() {
    return FieldMask(0);
  }

  static constexpr FieldMask Of(std::initializer_list<uint32_t> indices) {
    uint64_t bits = 0;
    for (auto index : indices) {
      if (index < 64) {
        bits |= 1ull << index;
      }
    }
    return FieldMask(bits);
  }

 private:
  constexpr explicit FieldMask(uint64_t bits) : bits(bits) {
  }
  uint64_t bits = 0;
};

}  // namespace tgfx

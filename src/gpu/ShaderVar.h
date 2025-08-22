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

#pragma once

#include <string>
#include <vector>
#include "BitmaskOperators.h"
#include "SLType.h"

namespace tgfx {
enum class ShaderFlags : unsigned {
  None = 0,
  Vertex = 1 << 0,
  Fragment = 1 << 1,
  TGFX_MARK_AS_BITMASK_ENUM(Fragment)
};

class ShaderVar {
 public:
  enum class TypeModifier {
    None,
    Attribute,
    Varying,
    FlatVarying,
    Uniform,
    Out,
  };

  ShaderVar() = default;

  ShaderVar(const std::string& name, SLType type) : _type(type), _name(name) {
  }

  ShaderVar(const std::string& name, SLType type, TypeModifier typeModifier)
      : _type(type), _typeModifier(typeModifier), _name(name) {
  }

  void setName(const std::string& name) {
    _name = name;
  }

  const std::string& name() const {
    return _name;
  }

  void setType(SLType type) {
    _type = type;
  }

  SLType type() const {
    return _type;
  }

  void setTypeModifier(TypeModifier type) {
    _typeModifier = type;
  }

  TypeModifier typeModifier() const {
    return _typeModifier;
  }

 private:
  SLType _type = SLType::Void;
  TypeModifier _typeModifier = TypeModifier::None;
  std::string _name;
};
}  // namespace tgfx

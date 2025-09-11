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

#include <utility>
#include <vector>
#include "SLType.h"
#include "gpu/Attribute.h"
#include "gpu/Uniform.h"

namespace tgfx {
class ShaderVar {
 public:
  enum class TypeModifier { None, Attribute, Varying, FlatVarying, Uniform, Out, InOut };

  ShaderVar() = default;

  ShaderVar(std::string name, SLType type, TypeModifier typeModifier = TypeModifier::None)
      : _name(std::move(name)), _type(type), _modifier(typeModifier) {
  }

  explicit ShaderVar(const Attribute& attribute);

  explicit ShaderVar(const Uniform& uniform);

  const std::string& name() const {
    return _name;
  }

  SLType type() const {
    return _type;
  }

  TypeModifier modifier() const {
    return _modifier;
  }

 private:
  std::string _name;
  SLType _type = SLType::Void;
  TypeModifier _modifier = TypeModifier::None;
};
}  // namespace tgfx

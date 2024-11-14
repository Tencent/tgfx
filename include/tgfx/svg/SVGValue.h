/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "tgfx/svg/SVGTypes.h"

namespace tgfx {

class SVGValue {
 public:
  enum class Type {
    kColor,
    kFilter,
    kLength,
    kNumber,
    kObjectBoundingBoxUnits,
    kPreserveAspectRatio,
    kStopColor,
    kString,
    kTransform,
    kViewBox,
  };

  Type type() const {
    return fType;
  }

  template <typename T>
  const T* as() const {
    return fType == T::_type ? static_cast<const T*>(this) : nullptr;
  }

 protected:
  explicit SVGValue(Type t) : fType(t) {
  }

 private:
  Type fType;
};

template <typename T, SVGValue::Type ValueType>
class SVGWrapperValue final : public SVGValue {
 public:
  static constexpr Type _type = ValueType;

  explicit SVGWrapperValue(const T& v) : INHERITED(ValueType), _wrappedValue(v) {
  }

  // NOLINTBEGIN
  // Allow implicit conversion to the wrapped type operator.
  operator const T&() const {
    return _wrappedValue;
  }
  // NOLINTEND

  const T* operator->() const {
    return &_wrappedValue;
  }

 private:
  // Stack-only
  void* operator new(size_t) = delete;
  void* operator new(size_t, void*) = delete;

  const T& _wrappedValue;

  using INHERITED = SVGValue;
};

using SVGColorValue = SVGWrapperValue<SVGColorType, SVGValue::Type::kColor>;
using SVGLengthValue = SVGWrapperValue<SVGLength, SVGValue::Type::kLength>;
using SVGTransformValue = SVGWrapperValue<SVGTransformType, SVGValue::Type::kTransform>;
using SVGViewBoxValue = SVGWrapperValue<SVGViewBoxType, SVGValue::Type::kViewBox>;
using SVGNumberValue = SVGWrapperValue<SVGNumberType, SVGValue::Type::kNumber>;
using SVGStringValue = SVGWrapperValue<SVGStringType, SVGValue::Type::kString>;
using SVGStopColorValue = SVGWrapperValue<SVGStopColor, SVGValue::Type::kStopColor>;

using SVGPreserveAspectRatioValue =
    SVGWrapperValue<SVGPreserveAspectRatio, SVGValue::Type::kPreserveAspectRatio>;

using SVGObjectBoundingBoxUnitsValue =
    SVGWrapperValue<SVGObjectBoundingBoxUnits, SVGValue::Type::kObjectBoundingBoxUnits>;
}  // namespace tgfx

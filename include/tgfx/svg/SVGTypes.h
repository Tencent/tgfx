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

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "tgfx/core/Color.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/PathTypes.h"
#include "tgfx/core/Point.h"
#include "tgfx/core/Rect.h"

// #define RENDER_SVG

namespace tgfx {

using SVGColorType = Color;
using SVGIntegerType = int;
using SVGNumberType = float;
using SVGStringType = std::string;
using SVGViewBoxType = Rect;
using SVGTransformType = Matrix;
using SVGPointsType = std::vector<Point>;

enum class SVGPropertyState {
  Unspecified,
  Inherit,
  Value,
};

// https://www.w3.org/TR/SVG11/intro.html#TermProperty
template <typename T, bool kInheritable>
class SVGProperty {
 public:
  using ValueT = T;

  SVGProperty() : _state(SVGPropertyState::Unspecified) {
  }

  explicit SVGProperty(SVGPropertyState state) : _state(state) {
  }

  explicit SVGProperty(const T& value) : _state(SVGPropertyState::Value) {
    _value = value;
  }

  explicit SVGProperty(T&& value) : _state(SVGPropertyState::Value) {
    _value = std::move(value);
  }

  template <typename... Args>
  void init(Args&&... args) {
    _state = SVGPropertyState::Value;
    _value.emplace(std::forward<Args>(args)...);
  }

  constexpr bool isInheritable() const {
    return kInheritable;
  }

  bool isValue() const {
    return _state == SVGPropertyState::Value;
  }

  T* getMaybeNull() const {
    return _value.has_value() ? &_value.value() : nullptr;
  }

  void set(SVGPropertyState state) {
    _state = state;
    if (_state != SVGPropertyState::Value) {
      _value.reset();
    }
  }

  void set(const T& value) {
    _state = SVGPropertyState::Value;
    _value = value;
  }

  void set(T&& value) {
    _state = SVGPropertyState::Value;
    _value = std::move(value);
  }

  T* operator->() {
    // ASSERT(fState == SVGPropertyState::kValue);
    // ASSERT(fValue.has_value());
    return &_value.value();
  }

  const T* operator->() const {
    // ASSERT(fState == SVGPropertyState::kValue);
    // ASSERT(fValue.has_value());
    return &_value.value();
  }

  T& operator*() {
    // ASSERT(fState == SVGPropertyState::kValue);
    // ASSERT(fValue.has_value());
    return *_value;
  }

  const T& operator*() const {
    // ASSERT(fState == SVGPropertyState::kValue);
    // ASSERT(fValue.has_value());
    return *_value;
  }

 private:
  SVGPropertyState _state;
  std::optional<T> _value;
};

class SVGLength {
 public:
  enum class Unit {
    Unknown,
    Number,
    Percentage,
    EMS,
    EXS,
    PX,
    CM,
    MM,
    IN,
    PT,
    PC,
  };

  constexpr SVGLength() : _value(0), _unit(Unit::Unknown) {
  }

  explicit constexpr SVGLength(float v, Unit u = Unit::Number) : _value(v), _unit(u) {
  }

  SVGLength(const SVGLength&) = default;

  SVGLength& operator=(const SVGLength&) = default;

  bool operator==(const SVGLength& other) const {
    return _unit == other._unit && _value == other._value;
  }

  bool operator!=(const SVGLength& other) const {
    return !(*this == other);
  }

  const float& value() const {
    return _value;
  }

  const Unit& unit() const {
    return _unit;
  }

 private:
  float _value;
  Unit _unit;
};

// https://www.w3.org/TR/SVG11/linking.html#IRIReference
class SVGIRI {
 public:
  enum class Type {
    Local,
    Nonlocal,
    DataURI,
  };

  SVGIRI() : _type(Type::Local) {
  }

  SVGIRI(Type t, SVGStringType iri) : _type(t), _iri(std::move(iri)) {
  }

  Type type() const {
    return _type;
  }

  const SVGStringType& iri() const {
    return _iri;
  }

  bool operator==(const SVGIRI& other) const {
    return _type == other._type && _iri == other._iri;
  }

  bool operator!=(const SVGIRI& other) const {
    return !(*this == other);
  }

 private:
  Type _type;
  SVGStringType _iri;
};

// https://www.w3.org/TR/SVG11/types.html#InterfaceSVGColor
class SVGColor {
 public:
  enum class Type {
    CurrentColor,
    Color,
    ICCColor,
  };

  using Vars = std::vector<std::string>;

  SVGColor() : SVGColor(Color::Black()) {
  }

  explicit SVGColor(const SVGColorType& c) : _type(Type::Color), _color(c), _vars(nullptr) {
  }

  SVGColor(Type t, Vars&& vars)
      : _type(t), _color(Color::Black()),
        _vars(vars.empty() ? nullptr : std::make_shared<Vars>(std::move(vars))) {
  }

  SVGColor(const SVGColorType& c, Vars&& vars)
      : _type(Type::Color), _color(c),
        _vars(vars.empty() ? nullptr : std::make_shared<Vars>(std::move(vars))) {
  }

  SVGColor(const SVGColor&) = default;
  SVGColor& operator=(const SVGColor&) = default;
  SVGColor(SVGColor&&) = default;
  SVGColor& operator=(SVGColor&&) = default;

  bool operator==(const SVGColor& other) const {
    return _type == other._type && _color == other._color && _vars == other._vars;
  }

  bool operator!=(const SVGColor& other) const {
    return !(*this == other);
  }

  Type type() const {
    return _type;
  }

  const SVGColorType& color() const {
    return _color;
  }

  std::shared_ptr<Vars> vars() const {
    return _vars ? _vars : nullptr;
  }

  std::shared_ptr<Vars> vars() {
    return _vars ? _vars : nullptr;
  }

 private:
  Type _type;
  SVGColorType _color;
  std::shared_ptr<Vars> _vars;
};

class SVGPaint {
 public:
  enum class Type {
    None,
    Color,
    IRI,
  };

  SVGPaint() : _type(Type::None), _color(Color::Black()) {
  }
  explicit SVGPaint(Type t) : _type(t), _color(Color::Black()) {
  }
  explicit SVGPaint(SVGColor c) : _type(Type::Color), _color(std::move(c)) {
  }
  SVGPaint(SVGIRI iri, SVGColor fallback_color)
      : _type(Type::IRI), _color(std::move(fallback_color)), _iri(std::move(iri)) {
  }

  SVGPaint(const SVGPaint&) = default;
  SVGPaint& operator=(const SVGPaint&) = default;
  SVGPaint(SVGPaint&&) = default;
  SVGPaint& operator=(SVGPaint&&) = default;

  bool operator==(const SVGPaint& other) const {
    return _type == other._type && _color == other._color && _iri == other._iri;
  }
  bool operator!=(const SVGPaint& other) const {
    return !(*this == other);
  }

  Type type() const {
    return _type;
  }
  const SVGColor& color() const {
    return _color;
  }
  const SVGIRI& iri() const {
    return _iri;
  }

 private:
  Type _type;
  SVGColor _color;
  SVGIRI _iri;
};

// <funciri> | none (used for clip/mask/filter properties)
class SVGFuncIRI {
 public:
  enum class Type {
    None,
    IRI,
  };

  SVGFuncIRI() : _type(Type::None) {
  }

  explicit SVGFuncIRI(Type t) : _type(t) {
  }

  explicit SVGFuncIRI(SVGIRI&& iri) : _type(Type::IRI), _iri(std::move(iri)) {
  }

  bool operator==(const SVGFuncIRI& other) const {
    return _type == other._type && _iri == other._iri;
  }

  bool operator!=(const SVGFuncIRI& other) const {
    return !(*this == other);
  }

  Type type() const {
    return _type;
  }

  const SVGIRI& iri() const {
    return _iri;
  }

 private:
  Type _type;
  SVGIRI _iri;
};

enum class SVGLineCap {
  Butt,
  Round,
  Square,
};

class SVGLineJoin {
 public:
  enum class Type {
    Miter,
    Round,
    Bevel,
    Inherit,
  };

  constexpr SVGLineJoin() : _type(Type::Inherit) {
  }
  constexpr explicit SVGLineJoin(Type t) : _type(t) {
  }

  SVGLineJoin(const SVGLineJoin&) = default;
  SVGLineJoin& operator=(const SVGLineJoin&) = default;

  bool operator==(const SVGLineJoin& other) const {
    return _type == other._type;
  }
  bool operator!=(const SVGLineJoin& other) const {
    return !(*this == other);
  }

  Type type() const {
    return _type;
  }

 private:
  Type _type;
};

class SVGSpreadMethod {
 public:
  enum class Type {
    Pad,
    Repeat,
    Reflect,
  };

  constexpr SVGSpreadMethod() : _type(Type::Pad) {
  }
  constexpr explicit SVGSpreadMethod(Type t) : _type(t) {
  }

  SVGSpreadMethod(const SVGSpreadMethod&) = default;
  SVGSpreadMethod& operator=(const SVGSpreadMethod&) = default;

  bool operator==(const SVGSpreadMethod& other) const {
    return _type == other._type;
  }
  bool operator!=(const SVGSpreadMethod& other) const {
    return !(*this == other);
  }

  Type type() const {
    return _type;
  }

 private:
  Type _type;
};

class SVGFillRule {
 public:
  enum class Type {
    NonZero,
    EvenOdd,
    Inherit,
  };

  constexpr SVGFillRule() : _type(Type::Inherit) {
  }
  constexpr explicit SVGFillRule(Type t) : _type(t) {
  }

  SVGFillRule(const SVGFillRule&) = default;
  SVGFillRule& operator=(const SVGFillRule&) = default;

  bool operator==(const SVGFillRule& other) const {
    return _type == other._type;
  }
  bool operator!=(const SVGFillRule& other) const {
    return !(*this == other);
  }

  Type type() const {
    return _type;
  }

  PathFillType asFillType() const {
    return _type == Type::EvenOdd ? PathFillType::EvenOdd : PathFillType::Winding;
  }

 private:
  Type _type;
};

class SVGVisibility {
 public:
  enum class Type {
    Visible,
    Hidden,
    Collapse,
    Inherit,
  };

  constexpr SVGVisibility() : _type(Type::Visible) {
  }
  constexpr explicit SVGVisibility(Type t) : _type(t) {
  }

  SVGVisibility(const SVGVisibility&) = default;
  SVGVisibility& operator=(const SVGVisibility&) = default;

  bool operator==(const SVGVisibility& other) const {
    return _type == other._type;
  }
  bool operator!=(const SVGVisibility& other) const {
    return !(*this == other);
  }

  Type type() const {
    return _type;
  }

 private:
  Type _type;
};

class SVGDashArray {
 public:
  enum class Type {
    None,
    DashArray,
    Inherit,
  };

  SVGDashArray() : _type(Type::None) {
  }
  explicit SVGDashArray(Type t) : _type(t) {
  }
  explicit SVGDashArray(std::vector<SVGLength>&& dashArray)
      : _type(Type::DashArray), _dashArray(std::move(dashArray)) {
  }

  SVGDashArray(const SVGDashArray&) = default;
  SVGDashArray& operator=(const SVGDashArray&) = default;

  bool operator==(const SVGDashArray& other) const {
    return _type == other._type && _dashArray == other._dashArray;
  }
  bool operator!=(const SVGDashArray& other) const {
    return !(*this == other);
  }

  Type type() const {
    return _type;
  }

  const std::vector<SVGLength>& dashArray() const {
    return _dashArray;
  }

 private:
  Type _type;
  std::vector<SVGLength> _dashArray;
};

class SVGStopColor {
 public:
  enum class Type {
    kColor,
    kCurrentColor,
    kICCColor,
    kInherit,
  };

  SVGStopColor() : _type(Type::kColor), _color(Color::Black()) {
  }
  explicit SVGStopColor(Type t) : _type(t), _color(Color::Black()) {
  }
  explicit SVGStopColor(const SVGColorType& c) : _type(Type::kColor), _color(c) {
  }

  SVGStopColor(const SVGStopColor&) = default;
  SVGStopColor& operator=(const SVGStopColor&) = default;

  bool operator==(const SVGStopColor& other) const {
    return _type == other._type && _color == other._color;
  }
  bool operator!=(const SVGStopColor& other) const {
    return !(*this == other);
  }

  Type type() const {
    return _type;
  }
  const SVGColorType& color() const {
    return _color;
  }

 private:
  Type _type;
  SVGColorType _color;
};

class SVGObjectBoundingBoxUnits {
 public:
  enum class Type {
    UserSpaceOnUse,
    ObjectBoundingBox,
  };

  SVGObjectBoundingBoxUnits() : _type(Type::UserSpaceOnUse) {
  }
  explicit SVGObjectBoundingBoxUnits(Type t) : _type(t) {
  }

  bool operator==(const SVGObjectBoundingBoxUnits& other) const {
    return _type == other._type;
  }
  bool operator!=(const SVGObjectBoundingBoxUnits& other) const {
    return !(*this == other);
  }

  Type type() const {
    return _type;
  }

 private:
  Type _type;
};

class SVGFontFamily {
 public:
  enum class Type {
    Family,
    Inherit,
  };

  SVGFontFamily() : _type(Type::Inherit) {
  }
  explicit SVGFontFamily(std::string family) : _type(Type::Family), _family(std::move(family)) {
  }

  bool operator==(const SVGFontFamily& other) const {
    return _type == other._type && _family == other._family;
  }
  bool operator!=(const SVGFontFamily& other) const {
    return !(*this == other);
  }

  Type type() const {
    return _type;
  }

  const std::string& family() const {
    return _family;
  }

 private:
  Type _type;
  std::string _family;
};

class SVGFontStyle {
 public:
  enum class Type {
    Normal,
    Italic,
    Oblique,
    Inherit,
  };

  SVGFontStyle() : _type(Type::Inherit) {
  }
  explicit SVGFontStyle(Type t) : _type(t) {
  }

  bool operator==(const SVGFontStyle& other) const {
    return _type == other._type;
  }
  bool operator!=(const SVGFontStyle& other) const {
    return !(*this == other);
  }

  Type type() const {
    return _type;
  }

 private:
  Type _type;
};

class SVGFontSize {
 public:
  enum class Type {
    Length,
    Inherit,
  };

  SVGFontSize() : _type(Type::Inherit), _size(0) {
  }
  explicit SVGFontSize(const SVGLength& s) : _type(Type::Length), _size(s) {
  }

  bool operator==(const SVGFontSize& other) const {
    return _type == other._type && _size == other._size;
  }
  bool operator!=(const SVGFontSize& other) const {
    return !(*this == other);
  }

  Type type() const {
    return _type;
  }

  const SVGLength& size() const {
    return _size;
  }

 private:
  Type _type;
  SVGLength _size;
};

class SVGFontWeight {
 public:
  enum class Type {
    W100,
    W200,
    W300,
    W400,
    W500,
    W600,
    W700,
    W800,
    W900,
    Normal,
    Bold,
    Bolder,
    Lighter,
    Inherit,
  };

  SVGFontWeight() : _type(Type::Inherit) {
  }
  explicit SVGFontWeight(Type t) : _type(t) {
  }

  bool operator==(const SVGFontWeight& other) const {
    return _type == other._type;
  }
  bool operator!=(const SVGFontWeight& other) const {
    return !(*this == other);
  }

  Type type() const {
    return _type;
  }

 private:
  Type _type;
};

struct SVGPreserveAspectRatio {
  enum Align : uint8_t {
    // These values are chosen such that bits [0,1] encode X alignment, and
    // bits [2,3] encode Y alignment.
    XMinYMin = 0x00,
    XMidYMin = 0x01,
    XMaxYMin = 0x02,
    XMinYMid = 0x04,
    XMidYMid = 0x05,
    XMaxYMid = 0x06,
    XMinYMax = 0x08,
    XMidYMax = 0x09,
    XMaxYMax = 0x0a,

    None = 0x10,
  };

  enum Scale {
    Meet,
    Slice,
  };

  Align align = XMidYMid;
  Scale scale = Meet;
};

class SVGTextAnchor {
 public:
  enum class Type {
    kStart,
    kMiddle,
    kEnd,
    kInherit,
  };

  SVGTextAnchor() : fType(Type::kInherit) {
  }
  explicit SVGTextAnchor(Type t) : fType(t) {
  }

  bool operator==(const SVGTextAnchor& other) const {
    return fType == other.fType;
  }
  bool operator!=(const SVGTextAnchor& other) const {
    return !(*this == other);
  }

  Type type() const {
    return fType;
  }

 private:
  Type fType;
};

// https://www.w3.org/TR/SVG11/filters.html#FilterPrimitiveInAttribute
class SVGFeInputType {
 public:
  enum class Type {
    kSourceGraphic,
    kSourceAlpha,
    kBackgroundImage,
    kBackgroundAlpha,
    kFillPaint,
    kStrokePaint,
    kFilterPrimitiveReference,
    kUnspecified,
  };

  SVGFeInputType() : fType(Type::kUnspecified) {
  }
  explicit SVGFeInputType(Type t) : fType(t) {
  }
  explicit SVGFeInputType(SVGStringType id)
      : fType(Type::kFilterPrimitiveReference), fId(std::move(id)) {
  }

  bool operator==(const SVGFeInputType& other) const {
    return fType == other.fType && fId == other.fId;
  }
  bool operator!=(const SVGFeInputType& other) const {
    return !(*this == other);
  }

  const std::string& id() const {
    return fId;
  }

  Type type() const {
    return fType;
  }

 private:
  Type fType;
  std::string fId;
};

enum class SVGFeColorMatrixType {
  kMatrix,
  kSaturate,
  kHueRotate,
  kLuminanceToAlpha,
};

using SVGFeColorMatrixValues = std::vector<SVGNumberType>;

enum class SVGFeCompositeOperator {
  kOver,
  kIn,
  kOut,
  kAtop,
  kXor,
  kArithmetic,
};

class SVGFeTurbulenceBaseFrequency {
 public:
  SVGFeTurbulenceBaseFrequency() : fFreqX(0), fFreqY(0) {
  }
  SVGFeTurbulenceBaseFrequency(SVGNumberType freqX, SVGNumberType freqY)
      : fFreqX(freqX), fFreqY(freqY) {
  }

  SVGNumberType freqX() const {
    return fFreqX;
  }
  SVGNumberType freqY() const {
    return fFreqY;
  }

 private:
  SVGNumberType fFreqX;
  SVGNumberType fFreqY;
};

struct SVGFeTurbulenceType {
  enum Type {
    kFractalNoise,
    kTurbulence,
  };

  Type fType;

  SVGFeTurbulenceType() : fType(kTurbulence) {
  }
  explicit SVGFeTurbulenceType(Type type) : fType(type) {
  }
};

enum class SVGColorspace {
  kAuto,
  kSRGB,
  kLinearRGB,
};

// https://www.w3.org/TR/SVG11/painting.html#DisplayProperty
enum class SVGDisplay {
  kInline,
  kNone,
};

// https://www.w3.org/TR/SVG11/filters.html#TransferFunctionElementAttributes
enum class SVGFeFuncType {
  kIdentity,
  kTable,
  kDiscrete,
  kLinear,
  kGamma,
};
}  // namespace tgfx
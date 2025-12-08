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

#ifdef IN
#undef IN
#endif

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
template <typename T, bool Inheritable>
class SVGProperty {
 public:
  using ValueT = T;

  SVGProperty() = default;

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
    return Inheritable;
  }

  bool isValue() const {
    return _state == SVGPropertyState::Value;
  }

  std::optional<T> get() const {
    return _value;
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
    return _value ? std::addressof(*_value) : nullptr;
  }

  const T* operator->() const {
    return _value ? std::addressof(*_value) : nullptr;
  }

  T& operator*() {
    return *_value;
  }

  const T& operator*() const {
    return *_value;
  }

 private:
  SVGPropertyState _state = SVGPropertyState::Unspecified;
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

  SVGLength() = default;

  explicit SVGLength(float v, Unit u = Unit::Number) : _value(v), _unit(u) {
  }

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
  float _value = 0.0f;
  Unit _unit = Unit::Unknown;
};

// https://www.w3.org/TR/SVG11/linking.html#IRIReference
class SVGIRI {
 public:
  enum class Type {
    Local,
    Nonlocal,
    DataURI,
  };

  SVGIRI() = default;

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
  Type _type = Type::Local;
  SVGStringType _iri = {};
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

  SVGColor() = default;

  explicit SVGColor(const SVGColorType& c) : _color(c) {
  }

  SVGColor(Type t, Vars&& vars)
      : _type(t), _vars(vars.empty() ? nullptr : std::make_shared<Vars>(std::move(vars))) {
  }

  SVGColor(const SVGColorType& c, Vars&& vars)
      : _color(c), _vars(vars.empty() ? nullptr : std::make_shared<Vars>(std::move(vars))) {
  }

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

 private:
  Type _type = Type::Color;
  SVGColorType _color = Color::Black();
  std::shared_ptr<Vars> _vars = nullptr;
};

class SVGPaint {
 public:
  enum class Type {
    None,
    Color,
    IRI,
  };

  SVGPaint() = default;

  explicit SVGPaint(Type t) : _type(t), _color(Color::Black()) {
  }

  explicit SVGPaint(const SVGColor& color) : _type(Type::Color), _color(std::move(color)) {
  }

  SVGPaint(SVGIRI iri, SVGColor color)
      : _type(Type::IRI), _color(std::move(color)), _iri(std::move(iri)) {
  }

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
  Type _type = Type::None;
  SVGColor _color = SVGColor(Color::Black());
  SVGIRI _iri = {};
};

// <funciri> | none (used for clip/mask/filter properties)
class SVGFuncIRI {
 public:
  enum class Type {
    None,
    IRI,
  };

  SVGFuncIRI() = default;

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
  Type _type = Type::None;
  SVGIRI _iri = {};
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

  SVGLineJoin() = default;

  explicit SVGLineJoin(Type t) : _type(t) {
  }

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
  Type _type = Type::Inherit;
};

class SVGSpreadMethod {
 public:
  enum class Type {
    Pad,
    Repeat,
    Reflect,
  };

  SVGSpreadMethod() = default;

  explicit SVGSpreadMethod(Type t) : _type(t) {
  }

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
  Type _type = Type::Pad;
};

class SVGFillRule {
 public:
  enum class Type {
    NonZero,
    EvenOdd,
    Inherit,
  };

  SVGFillRule() = default;

  explicit SVGFillRule(Type t) : _type(t) {
  }

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
  Type _type = Type::Inherit;
};

class SVGVisibility {
 public:
  enum class Type {
    Visible,
    Hidden,
    Collapse,
    Inherit,
  };

  SVGVisibility() = default;

  explicit SVGVisibility(Type t) : _type(t) {
  }

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
  Type _type = Type::Visible;
};

class SVGDashArray {
 public:
  enum class Type {
    None,
    DashArray,
    Inherit,
  };

  SVGDashArray() = default;

  explicit SVGDashArray(Type t) : _type(t) {
  }

  explicit SVGDashArray(std::vector<SVGLength>&& dashArray)
      : _type(Type::DashArray), _dashArray(std::move(dashArray)) {
  }

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
  Type _type = Type::None;
  std::vector<SVGLength> _dashArray;
};

class SVGStopColor {
 public:
  enum class Type {
    Color,
    CurrentColor,
    ICCColor,
    Inherit,
  };

  SVGStopColor() = default;

  explicit SVGStopColor(Type t) : _type(t) {
  }

  explicit SVGStopColor(const SVGColorType& c) : _color(c) {
  }

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
  Type _type = Type::Color;
  SVGColorType _color = Color::Black();
};

class SVGObjectBoundingBoxUnits {
 public:
  enum class Type {
    UserSpaceOnUse,
    ObjectBoundingBox,
  };

  SVGObjectBoundingBoxUnits() = default;

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
  Type _type = Type::UserSpaceOnUse;
};

class SVGFontFamily {
 public:
  enum class Type {
    Family,
    Inherit,
  };

  SVGFontFamily() = default;

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
  Type _type = Type::Inherit;
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

  SVGFontStyle() = default;

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
  Type _type = Type::Inherit;
};

class SVGFontSize {
 public:
  enum class Type {
    Length,
    Inherit,
  };

  SVGFontSize() = default;

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
  Type _type = Type::Inherit;
  SVGLength _size = SVGLength(0.0f);
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

  SVGFontWeight() = default;

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
  Type _type = Type::Inherit;
};

struct SVGPreserveAspectRatio {
  enum class Align {
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

  enum class Scale {
    Meet,
    Slice,
  };

  Align align = Align::XMidYMid;
  Scale scale = Scale::Meet;
};

class SVGTextAnchor {
 public:
  enum class Type {
    Start,
    Middle,
    End,
    Inherit,
  };

  SVGTextAnchor() = default;

  explicit SVGTextAnchor(Type t) : _type(t) {
  }

  bool operator==(const SVGTextAnchor& other) const {
    return _type == other._type;
  }

  bool operator!=(const SVGTextAnchor& other) const {
    return !(*this == other);
  }

  Type type() const {
    return _type;
  }

  float getAlignmentFactor() const {
    switch (_type) {
      case SVGTextAnchor::Type::Start:
        return 0.0f;
      case SVGTextAnchor::Type::Middle:
        return -0.5f;
      case SVGTextAnchor::Type::End:
        return -1.0f;
      case SVGTextAnchor::Type::Inherit:
        return 0.0f;
    }
  }

 private:
  Type _type = Type::Inherit;
};

// https://www.w3.org/TR/SVG11/filters.html#FilterPrimitiveInAttribute
class SVGFeInputType {
 public:
  enum class Type {
    SourceGraphic,
    SourceAlpha,
    BackgroundImage,
    BackgroundAlpha,
    FillPaint,
    StrokePaint,
    FilterPrimitiveReference,
    Unspecified,
  };

  SVGFeInputType() = default;

  explicit SVGFeInputType(Type t) : _type(t) {
  }

  explicit SVGFeInputType(SVGStringType id)
      : _type(Type::FilterPrimitiveReference), _id(std::move(id)) {
  }

  bool operator==(const SVGFeInputType& other) const {
    return _type == other._type && _id == other._id;
  }

  bool operator!=(const SVGFeInputType& other) const {
    return !(*this == other);
  }

  const std::string& id() const {
    return _id;
  }

  Type type() const {
    return _type;
  }

 private:
  Type _type = Type::Unspecified;
  std::string _id;
};

enum class SVGFeColorMatrixType {
  Matrix,
  Saturate,
  HueRotate,
  LuminanceToAlpha,
};

using SVGFeColorMatrixValues = std::vector<SVGNumberType>;

enum class SVGFeCompositeOperator {
  Over,
  In,
  Out,
  Atop,
  Xor,
  Arithmetic,
};

class SVGFeTurbulenceBaseFrequency {
 public:
  SVGFeTurbulenceBaseFrequency() = default;

  SVGFeTurbulenceBaseFrequency(SVGNumberType freqX, SVGNumberType freqY)
      : _freqX(freqX), _freqY(freqY) {
  }

  SVGNumberType freqX() const {
    return _freqX;
  }

  SVGNumberType freqY() const {
    return _freqY;
  }

 private:
  SVGNumberType _freqX = 0.0f;
  SVGNumberType _freqY = 0.0f;
};

struct SVGFeTurbulenceType {
  enum class Type {
    FractalNoise,
    Turbulence,
  };

  Type type = Type::Turbulence;

  SVGFeTurbulenceType() = default;

  explicit SVGFeTurbulenceType(Type inputType) : type(inputType) {
  }
};

enum class SVGXmlSpace {
  Default,
  Preserve,
};

enum class SVGColorspace {
  Auto,
  SRGB,
  LinearRGB,
};

// https://www.w3.org/TR/SVG11/painting.html#DisplayProperty
enum class SVGDisplay {
  Inline,
  None,
};

// https://www.w3.org/TR/SVG11/filters.html#TransferFunctionElementAttributes
enum class SVGFeFuncType {
  Identity,
  Table,
  Discrete,
  Linear,
  Gamma,
};

class SVGMaskType {
 public:
  enum class Type {
    Luminance,
    Alpha,
  };

  SVGMaskType() = default;

  explicit SVGMaskType(Type t) : _type(t) {
  }

  bool operator==(const SVGMaskType& other) const {
    return _type == other._type;
  }

  bool operator!=(const SVGMaskType& other) const {
    return !(*this == other);
  }

  Type type() const {
    return _type;
  }

 private:
  Type _type = Type::Luminance;
};

}  // namespace tgfx
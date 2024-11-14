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
  kUnspecified,
  kInherit,
  kValue,
};

// https://www.w3.org/TR/SVG11/intro.html#TermProperty
template <typename T, bool kInheritable>
class SVGProperty {
 public:
  using ValueT = T;

  SVGProperty() : fState(SVGPropertyState::kUnspecified) {
  }

  explicit SVGProperty(SVGPropertyState state) : fState(state) {
  }

  explicit SVGProperty(const T& value) : fState(SVGPropertyState::kValue) {
    fValue = value;
  }

  explicit SVGProperty(T&& value) : fState(SVGPropertyState::kValue) {
    fValue = std::move(value);
  }

  template <typename... Args>
  void init(Args&&... args) {
    fState = SVGPropertyState::kValue;
    fValue.emplace(std::forward<Args>(args)...);
  }

  constexpr bool isInheritable() const {
    return kInheritable;
  }

  bool isValue() const {
    return fState == SVGPropertyState::kValue;
  }

  T* getMaybeNull() const {
    return fValue.has_value() ? &fValue.value() : nullptr;
  }

  void set(SVGPropertyState state) {
    fState = state;
    if (fState != SVGPropertyState::kValue) {
      fValue.reset();
    }
  }

  void set(const T& value) {
    fState = SVGPropertyState::kValue;
    fValue = value;
  }

  void set(T&& value) {
    fState = SVGPropertyState::kValue;
    fValue = std::move(value);
  }

  T* operator->() {
    // ASSERT(fState == SVGPropertyState::kValue);
    // ASSERT(fValue.has_value());
    return &fValue.value();
  }

  const T* operator->() const {
    // ASSERT(fState == SVGPropertyState::kValue);
    // ASSERT(fValue.has_value());
    return &fValue.value();
  }

  T& operator*() {
    // ASSERT(fState == SVGPropertyState::kValue);
    // ASSERT(fValue.has_value());
    return *fValue;
  }

  const T& operator*() const {
    // ASSERT(fState == SVGPropertyState::kValue);
    // ASSERT(fValue.has_value());
    return *fValue;
  }

 private:
  SVGPropertyState fState;
  std::optional<T> fValue;
};

class SVGLength {
 public:
  enum class Unit {
    kUnknown,
    kNumber,
    kPercentage,
    kEMS,
    kEXS,
    kPX,
    kCM,
    kMM,
    kIN,
    kPT,
    kPC,
  };

  constexpr SVGLength() : fValue(0), fUnit(Unit::kUnknown) {
  }
  explicit constexpr SVGLength(float v, Unit u = Unit::kNumber) : fValue(v), fUnit(u) {
  }
  SVGLength(const SVGLength&) = default;
  SVGLength& operator=(const SVGLength&) = default;

  bool operator==(const SVGLength& other) const {
    return fUnit == other.fUnit && fValue == other.fValue;
  }
  bool operator!=(const SVGLength& other) const {
    return !(*this == other);
  }

  const float& value() const {
    return fValue;
  }
  const Unit& unit() const {
    return fUnit;
  }

 private:
  float fValue;
  Unit fUnit;
};

// https://www.w3.org/TR/SVG11/linking.html#IRIReference
class SVGIRI {
 public:
  enum class Type {
    kLocal,
    kNonlocal,
    kDataURI,
  };

  SVGIRI() : fType(Type::kLocal) {
  }
  SVGIRI(Type t, SVGStringType iri) : fType(t), fIRI(std::move(iri)) {
  }

  Type type() const {
    return fType;
  }
  const SVGStringType& iri() const {
    return fIRI;
  }

  bool operator==(const SVGIRI& other) const {
    return fType == other.fType && fIRI == other.fIRI;
  }
  bool operator!=(const SVGIRI& other) const {
    return !(*this == other);
  }

 private:
  Type fType;
  SVGStringType fIRI;
};

// https://www.w3.org/TR/SVG11/types.html#InterfaceSVGColor
class SVGColor {
 public:
  enum class Type {
    kCurrentColor,
    kColor,
    kICCColor,
  };
  using Vars = std::vector<std::string>;

  SVGColor() : SVGColor(Color::Black()) {
  }
  explicit SVGColor(const SVGColorType& c) : fType(Type::kColor), fColor(c), fVars(nullptr) {
  }
  explicit SVGColor(Type t, Vars&& vars)
      : fType(t), fColor(Color::Black()),
        fVars(vars.empty() ? nullptr : std::make_shared<Vars>(std::move(vars))) {
  }
  explicit SVGColor(const SVGColorType& c, Vars&& vars)
      : fType(Type::kColor), fColor(c),
        fVars(vars.empty() ? nullptr : std::make_shared<Vars>(std::move(vars))) {
  }

  SVGColor(const SVGColor&) = default;
  SVGColor& operator=(const SVGColor&) = default;
  SVGColor(SVGColor&&) = default;
  SVGColor& operator=(SVGColor&&) = default;

  bool operator==(const SVGColor& other) const {
    return fType == other.fType && fColor == other.fColor && fVars == other.fVars;
  }
  bool operator!=(const SVGColor& other) const {
    return !(*this == other);
  }

  Type type() const {
    return fType;
  }
  const SVGColorType& color() const {
    return fColor;
  }
  const std::shared_ptr<Vars> vars() const {
    return fVars ? fVars : nullptr;
  }

  std::shared_ptr<Vars> vars() {
    return fVars ? fVars : nullptr;
  }

 private:
  Type fType;
  SVGColorType fColor;
  std::shared_ptr<Vars> fVars;
};

class SVGPaint {
 public:
  enum class Type {
    kNone,
    kColor,
    kIRI,
  };

  SVGPaint() : fType(Type::kNone), fColor(Color::Black()) {
  }
  explicit SVGPaint(Type t) : fType(t), fColor(Color::Black()) {
  }
  explicit SVGPaint(SVGColor c) : fType(Type::kColor), fColor(std::move(c)) {
  }
  SVGPaint(SVGIRI iri, SVGColor fallback_color)
      : fType(Type::kIRI), fColor(std::move(fallback_color)), fIRI(std::move(iri)) {
  }

  SVGPaint(const SVGPaint&) = default;
  SVGPaint& operator=(const SVGPaint&) = default;
  SVGPaint(SVGPaint&&) = default;
  SVGPaint& operator=(SVGPaint&&) = default;

  bool operator==(const SVGPaint& other) const {
    return fType == other.fType && fColor == other.fColor && fIRI == other.fIRI;
  }
  bool operator!=(const SVGPaint& other) const {
    return !(*this == other);
  }

  Type type() const {
    return fType;
  }
  const SVGColor& color() const {
    return fColor;
  }
  const SVGIRI& iri() const {
    return fIRI;
  }

 private:
  Type fType;

  // Logical union.
  SVGColor fColor;
  SVGIRI fIRI;
};

// <funciri> | none (used for clip/mask/filter properties)
class SVGFuncIRI {
 public:
  enum class Type {
    kNone,
    kIRI,
  };

  SVGFuncIRI() : fType(Type::kNone) {
  }
  explicit SVGFuncIRI(Type t) : fType(t) {
  }
  explicit SVGFuncIRI(SVGIRI&& iri) : fType(Type::kIRI), fIRI(std::move(iri)) {
  }

  bool operator==(const SVGFuncIRI& other) const {
    return fType == other.fType && fIRI == other.fIRI;
  }
  bool operator!=(const SVGFuncIRI& other) const {
    return !(*this == other);
  }

  Type type() const {
    return fType;
  }
  const SVGIRI& iri() const {
    return fIRI;
  }

 private:
  Type fType;
  SVGIRI fIRI;
};

enum class SVGLineCap {
  kButt,
  kRound,
  kSquare,
};

class SVGLineJoin {
 public:
  enum class Type {
    kMiter,
    kRound,
    kBevel,
    kInherit,
  };

  constexpr SVGLineJoin() : fType(Type::kInherit) {
  }
  constexpr explicit SVGLineJoin(Type t) : fType(t) {
  }

  SVGLineJoin(const SVGLineJoin&) = default;
  SVGLineJoin& operator=(const SVGLineJoin&) = default;

  bool operator==(const SVGLineJoin& other) const {
    return fType == other.fType;
  }
  bool operator!=(const SVGLineJoin& other) const {
    return !(*this == other);
  }

  Type type() const {
    return fType;
  }

 private:
  Type fType;
};

class SVGSpreadMethod {
 public:
  enum class Type {
    kPad,
    kRepeat,
    kReflect,
  };

  constexpr SVGSpreadMethod() : fType(Type::kPad) {
  }
  constexpr explicit SVGSpreadMethod(Type t) : fType(t) {
  }

  SVGSpreadMethod(const SVGSpreadMethod&) = default;
  SVGSpreadMethod& operator=(const SVGSpreadMethod&) = default;

  bool operator==(const SVGSpreadMethod& other) const {
    return fType == other.fType;
  }
  bool operator!=(const SVGSpreadMethod& other) const {
    return !(*this == other);
  }

  Type type() const {
    return fType;
  }

 private:
  Type fType;
};

class SVGFillRule {
 public:
  enum class Type {
    kNonZero,
    kEvenOdd,
    kInherit,
  };

  constexpr SVGFillRule() : fType(Type::kInherit) {
  }
  constexpr explicit SVGFillRule(Type t) : fType(t) {
  }

  SVGFillRule(const SVGFillRule&) = default;
  SVGFillRule& operator=(const SVGFillRule&) = default;

  bool operator==(const SVGFillRule& other) const {
    return fType == other.fType;
  }
  bool operator!=(const SVGFillRule& other) const {
    return !(*this == other);
  }

  Type type() const {
    return fType;
  }

  PathFillType asFillType() const {
    return fType == Type::kEvenOdd ? PathFillType::EvenOdd : PathFillType::Winding;
  }

 private:
  Type fType;
};

class SVGVisibility {
 public:
  enum class Type {
    kVisible,
    kHidden,
    kCollapse,
    kInherit,
  };

  constexpr SVGVisibility() : fType(Type::kVisible) {
  }
  constexpr explicit SVGVisibility(Type t) : fType(t) {
  }

  SVGVisibility(const SVGVisibility&) = default;
  SVGVisibility& operator=(const SVGVisibility&) = default;

  bool operator==(const SVGVisibility& other) const {
    return fType == other.fType;
  }
  bool operator!=(const SVGVisibility& other) const {
    return !(*this == other);
  }

  Type type() const {
    return fType;
  }

 private:
  Type fType;
};

class SVGDashArray {
 public:
  enum class Type {
    kNone,
    kDashArray,
    kInherit,
  };

  SVGDashArray() : fType(Type::kNone) {
  }
  explicit SVGDashArray(Type t) : fType(t) {
  }
  explicit SVGDashArray(std::vector<SVGLength>&& dashArray)
      : fType(Type::kDashArray), fDashArray(std::move(dashArray)) {
  }

  SVGDashArray(const SVGDashArray&) = default;
  SVGDashArray& operator=(const SVGDashArray&) = default;

  bool operator==(const SVGDashArray& other) const {
    return fType == other.fType && fDashArray == other.fDashArray;
  }
  bool operator!=(const SVGDashArray& other) const {
    return !(*this == other);
  }

  Type type() const {
    return fType;
  }

  const std::vector<SVGLength>& dashArray() const {
    return fDashArray;
  }

 private:
  Type fType;
  std::vector<SVGLength> fDashArray;
};

class SVGStopColor {
 public:
  enum class Type {
    kColor,
    kCurrentColor,
    kICCColor,
    kInherit,
  };

  SVGStopColor() : fType(Type::kColor), fColor(Color::Black()) {
  }
  explicit SVGStopColor(Type t) : fType(t), fColor(Color::Black()) {
  }
  explicit SVGStopColor(const SVGColorType& c) : fType(Type::kColor), fColor(c) {
  }

  SVGStopColor(const SVGStopColor&) = default;
  SVGStopColor& operator=(const SVGStopColor&) = default;

  bool operator==(const SVGStopColor& other) const {
    return fType == other.fType && fColor == other.fColor;
  }
  bool operator!=(const SVGStopColor& other) const {
    return !(*this == other);
  }

  Type type() const {
    return fType;
  }
  const SVGColorType& color() const {
    return fColor;
  }

 private:
  Type fType;
  SVGColorType fColor;
};

class SVGObjectBoundingBoxUnits {
 public:
  enum class Type {
    kUserSpaceOnUse,
    kObjectBoundingBox,
  };

  SVGObjectBoundingBoxUnits() : fType(Type::kUserSpaceOnUse) {
  }
  explicit SVGObjectBoundingBoxUnits(Type t) : fType(t) {
  }

  bool operator==(const SVGObjectBoundingBoxUnits& other) const {
    return fType == other.fType;
  }
  bool operator!=(const SVGObjectBoundingBoxUnits& other) const {
    return !(*this == other);
  }

  Type type() const {
    return fType;
  }

 private:
  Type fType;
};

class SVGFontFamily {
 public:
  enum class Type {
    kFamily,
    kInherit,
  };

  SVGFontFamily() : fType(Type::kInherit) {
  }
  explicit SVGFontFamily(const char family[]) : fType(Type::kFamily), fFamily(family) {
  }

  bool operator==(const SVGFontFamily& other) const {
    return fType == other.fType && fFamily == other.fFamily;
  }
  bool operator!=(const SVGFontFamily& other) const {
    return !(*this == other);
  }

  Type type() const {
    return fType;
  }

  const std::string& family() const {
    return fFamily;
  }

 private:
  Type fType;
  std::string fFamily;
};

class SVGFontStyle {
 public:
  enum class Type {
    kNormal,
    kItalic,
    kOblique,
    kInherit,
  };

  SVGFontStyle() : fType(Type::kInherit) {
  }
  explicit SVGFontStyle(Type t) : fType(t) {
  }

  bool operator==(const SVGFontStyle& other) const {
    return fType == other.fType;
  }
  bool operator!=(const SVGFontStyle& other) const {
    return !(*this == other);
  }

  Type type() const {
    return fType;
  }

 private:
  Type fType;
};

class SVGFontSize {
 public:
  enum class Type {
    kLength,
    kInherit,
  };

  SVGFontSize() : fType(Type::kInherit), fSize(0) {
  }
  explicit SVGFontSize(const SVGLength& s) : fType(Type::kLength), fSize(s) {
  }

  bool operator==(const SVGFontSize& other) const {
    return fType == other.fType && fSize == other.fSize;
  }
  bool operator!=(const SVGFontSize& other) const {
    return !(*this == other);
  }

  Type type() const {
    return fType;
  }

  const SVGLength& size() const {
    return fSize;
  }

 private:
  Type fType;
  SVGLength fSize;
};

class SVGFontWeight {
 public:
  enum class Type {
    k100,
    k200,
    k300,
    k400,
    k500,
    k600,
    k700,
    k800,
    k900,
    kNormal,
    kBold,
    kBolder,
    kLighter,
    kInherit,
  };

  SVGFontWeight() : fType(Type::kInherit) {
  }
  explicit SVGFontWeight(Type t) : fType(t) {
  }

  bool operator==(const SVGFontWeight& other) const {
    return fType == other.fType;
  }
  bool operator!=(const SVGFontWeight& other) const {
    return !(*this == other);
  }

  Type type() const {
    return fType;
  }

 private:
  Type fType;
};

struct SVGPreserveAspectRatio {
  enum Align : uint8_t {
    // These values are chosen such that bits [0,1] encode X alignment, and
    // bits [2,3] encode Y alignment.
    kXMinYMin = 0x00,
    kXMidYMin = 0x01,
    kXMaxYMin = 0x02,
    kXMinYMid = 0x04,
    kXMidYMid = 0x05,
    kXMaxYMid = 0x06,
    kXMinYMax = 0x08,
    kXMidYMax = 0x09,
    kXMaxYMax = 0x0a,

    kNone = 0x10,
  };

  enum Scale {
    kMeet,
    kSlice,
  };

  Align fAlign = kXMidYMid;
  Scale fScale = kMeet;
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

//https://www.w3.org/TR/SVG2/pservers.html#PatternElementPatternUnitsAttribute
enum class SVGPatternUnits {
  UserSpaceOnUse,
  ObjectBoundingBox,
};
}  // namespace tgfx
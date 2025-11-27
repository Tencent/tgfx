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

#include "SVGAttributeParser.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "svg/SVGUtils.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/Typeface.h"
#include "tgfx/core/UTF.h"
#include "tgfx/svg/SVGTypes.h"

namespace {

inline bool is_between(char c, char min, char max) {
  ASSERT(min <= max);
  return static_cast<unsigned>(c - min) <= static_cast<unsigned>(max - min);
}

inline bool is_ws(char c) {
  return is_between(c, 1, 32);
}

inline bool is_sep(char c) {
  return is_ws(c) || c == ',' || c == ';';
}

inline bool is_nl(char c) {
  return c == '\n' || c == '\r' || c == '\f';
}

inline bool is_hex(char c) {
  return is_between(c, 'a', 'f') || is_between(c, 'A', 'F') || is_between(c, '0', '9');
}

}  // namespace

namespace tgfx {

SVGAttributeParser::SVGAttributeParser(const char* str, size_t length)
    : currentPos(str), endPos(str + length) {
}

template <typename F>
inline bool SVGAttributeParser::advanceWhile(F f) {
  const auto initial = currentPos;
  while (currentPos < endPos && f(*currentPos)) {
    currentPos++;
  }
  return currentPos != initial;
}

bool SVGAttributeParser::matchStringToken(const char* token, const char** newPos) const {
  const char* c = currentPos;

  while (c < endPos && *token && *c == *token) {
    c++;
    token++;
  }

  if (*token) {
    return false;
  }

  if (newPos) {
    *newPos = c;
  }

  return true;
}

bool SVGAttributeParser::parseEOSToken() {
  return currentPos == endPos;
}

bool SVGAttributeParser::parseSepToken() {
  return this->advanceWhile(is_sep);
}

bool SVGAttributeParser::parseWSToken() {
  return this->advanceWhile(is_ws);
}

bool SVGAttributeParser::parseCommaWspToken() {
  // comma-wsp:
  //     (wsp+ comma? wsp*) | (comma wsp*)
  return this->parseWSToken() || this->parseExpectedStringToken(",");
}

bool SVGAttributeParser::parseExpectedStringToken(const char* expected) {
  const char* newPos;
  if (!matchStringToken(expected, &newPos)) {
    return false;
  }

  currentPos = newPos;
  return true;
}

bool SVGAttributeParser::parseScalarToken(float* res) {
  if (const char* next = SVGParse::FindScalar(currentPos, res)) {
    currentPos = next;
    return true;
  }
  return false;
}

bool SVGAttributeParser::parseInt32Token(int32_t* res) {
  if (const char* next = SVGParse::FindS32(currentPos, res)) {
    currentPos = next;
    return true;
  }
  return false;
}

bool SVGAttributeParser::matchHexToken(const char** newPos) const {
  *newPos = currentPos;
  while (*newPos < endPos && is_hex(**newPos)) {
    ++*newPos;
  }
  return *newPos != currentPos;
}

bool SVGAttributeParser::parseEscape(Unichar* c) {
  // \(hexDigit{1,6}whitespace?|[^newline|hexDigit])
  RestoreCurPos restoreCurPos(this);

  if (!this->parseExpectedStringToken("\\")) {
    return false;
  }
  const char* hexEnd;
  if (this->matchHexToken(&hexEnd)) {
    if (hexEnd - currentPos > 6) {
      hexEnd = currentPos + 6;
    }
    char hexString[7];
    auto hexSize = static_cast<uint32_t>(hexEnd - currentPos);
    memcpy(hexString, currentPos, hexSize);
    hexString[hexSize] = '\0';
    uint32_t cp;
    const char* hexFound = SVGParse::FindHex(hexString, &cp);
    if (!hexFound || cp < 1 || (0xD800 <= cp && cp <= 0xDFFF) || 0x10FFFF < cp) {
      cp = 0xFFFD;
    }
    *c = static_cast<Unichar>(cp);
    currentPos = hexEnd;
    this->parseWSToken();
  } else if (this->parseEOSToken() || is_nl(*currentPos)) {
    *c = 0xFFFD;
    return false;
  } else {
    if (*c = UTF::NextUTF8(&currentPos, endPos); *c < 0) {
      return false;
    }
  }

  restoreCurPos.clear();
  return true;
}

bool SVGAttributeParser::parseIdentToken(std::string* ident) {
  // <ident-token>
  // (--|-?([a-z|A-Z|_|non-ASCII]|escape))([a-z|A-Z|0-9|_|-|non-ASCII]|escape)?
  RestoreCurPos restoreCurPos(this);

  Unichar c;
  if (this->parseExpectedStringToken("--")) {
    ident->append("--");
  } else {
    if (this->parseExpectedStringToken("-")) {
      ident->append("-");
    }
    if (this->parseEscape(&c)) {
      // ident->appendUnichar(c);
    } else {
      if (c = UTF::NextUTF8(&currentPos, endPos); c < 0) {
        return false;
      }
      if ((c < 'a' || 'z' < c) && (c < 'A' || 'Z' < c) && (c != '_') &&
          (c < 0x80 || 0x10FFFF < c)) {
        return false;
      }
      ident->append(UTF::ToUTF8(c));
    }
  }
  while (currentPos < endPos) {
    if (this->parseEscape(&c)) {
      ident->append(UTF::ToUTF8(c));
      continue;
    }
    const char* next = currentPos;
    if (c = UTF::NextUTF8(&next, endPos); c < 0) {
      break;
    }
    if ((c < 'a' || 'z' < c) && (c < 'A' || 'Z' < c) && (c < '0' || '9' < c) && (c != '_') &&
        (c != '-') && (c < 0x80 || 0x10FFFF < c)) {
      break;
    }
    ident->append(UTF::ToUTF8(c));
    currentPos = next;
  }

  restoreCurPos.clear();
  return true;
}

bool SVGAttributeParser::parseLengthUnitToken(SVGLength::Unit& unit) {
  struct Unit {
    Unit(std::string name, SVGLength::Unit u) : unitName(std::move(name)), unit(u) {
    }
    std::string unitName;
    SVGLength::Unit unit;
  };

  static const std::array<Unit, 9> unitInfo = {
      Unit("%", SVGLength::Unit::Percentage), Unit("em", SVGLength::Unit::EMS),
      Unit("ex", SVGLength::Unit::EXS),       Unit("px", SVGLength::Unit::PX),
      Unit("cm", SVGLength::Unit::CM),        Unit("mm", SVGLength::Unit::MM),
      Unit("in", SVGLength::Unit::Inches),    Unit("pt", SVGLength::Unit::PT),
      Unit("pc", SVGLength::Unit::PC),
  };

  return std::any_of(unitInfo.begin(), unitInfo.end(), [&](const Unit& item) {
    if (this->parseExpectedStringToken(item.unitName.data())) {
      unit = item.unit;
      return true;
    }
    return false;
  });
}

// https://www.w3.org/TR/SVG11/types.html#DataTypeColor
bool SVGAttributeParser::parseNamedColorToken(Color* c) {
  RestoreCurPos restoreCurPos(this);

  std::string ident;
  if (!this->parseIdentToken(&ident)) {
    return false;
  }
  if (!SVGParse::FindNamedColor(ident.c_str(), c)) {
    return false;
  }

  restoreCurPos.clear();
  return true;
}

bool SVGAttributeParser::parseHexColorToken(Color* c) {
  RestoreCurPos restoreCurPos(this);

  const char* hexEnd;
  if (!this->parseExpectedStringToken("#") || !this->matchHexToken(&hexEnd)) {
    return false;
  }

  uint32_t v;
  std::string hexString(currentPos, static_cast<uint32_t>(hexEnd - currentPos));
  SVGParse::FindHex(hexString.c_str(), &v);

  switch (hexString.size()) {
    case 6:
      // matched #xxxxxxx
      break;
    case 3:
      // matched '#xxx;
      v = ((v << 12) & 0x00f00000) | ((v << 8) & 0x000ff000) | ((v << 4) & 0x00000ff0) |
          ((v << 0) & 0x0000000f);
      break;
    default:
      return false;
  }

  *c = Uint32ToColor(v | 0xff000000);
  currentPos = hexEnd;

  restoreCurPos.clear();
  return true;
}

bool SVGAttributeParser::parseColorComponentIntegralToken(int32_t* c) {
  const char* p = SVGParse::FindS32(currentPos, c);
  if (!p || *p == '.') {
    // No value parsed, or fractional value.
    return false;
  }

  if (*p == '%') {
    *c = static_cast<int32_t>(std::round(static_cast<float>(*c) * 255.0f / 100));
    *c = std::clamp(*c, 0, 255);
    p++;
  }

  currentPos = p;
  return true;
}

bool SVGAttributeParser::parseColorComponentFractionalToken(int32_t* c) {
  float s;
  const char* p = SVGParse::FindScalar(currentPos, &s);
  if (!p || *p != '%') {
    // Floating point must be a percentage (CSS2 rgb-percent syntax).
    return false;
  }
  p++;  // Skip '%'

  *c = static_cast<int32_t>(std::round(s * 255.0f / 100));
  *c = std::clamp(*c, 0, 255);
  currentPos = p;
  return true;
}

bool SVGAttributeParser::parseColorComponentScalarToken(int32_t* c) {
  float s;
  if (const char* p = SVGParse::FindScalar(currentPos, &s)) {
    *c = static_cast<int32_t>(std::round(s * 255.0f));
    *c = std::clamp(*c, 0, 255);
    currentPos = p;
    return true;
  }
  return false;
}

bool SVGAttributeParser::parseColorComponentToken(int32_t* c) {
  return parseColorComponentIntegralToken(c) || parseColorComponentFractionalToken(c);
}

bool SVGAttributeParser::parseRGBColorToken(Color* c) {
  return this->parseParenthesized(
      "rgb",
      [this](Color* c) -> bool {
        int32_t r = 0;
        int32_t g = 0;
        int32_t b = 0;
        if (this->parseColorComponentToken(&r) && this->parseSepToken() &&
            this->parseColorComponentToken(&g) && this->parseSepToken() &&
            this->parseColorComponentToken(&b)) {

          *c = Color::FromRGBA(static_cast<uint8_t>(r), static_cast<uint8_t>(g),
                               static_cast<uint8_t>(b));
          return true;
        }
        return false;
      },
      c);
}

bool SVGAttributeParser::parseRGBAColorToken(Color* c) {
  return this->parseParenthesized(
      "rgba",
      [this](Color* c) -> bool {
        int32_t r = 0;
        int32_t g = 0;
        int32_t b = 0;
        int32_t a = 0;
        if (this->parseColorComponentToken(&r) && this->parseSepToken() &&
            this->parseColorComponentToken(&g) && this->parseSepToken() &&
            this->parseColorComponentToken(&b) && this->parseSepToken() &&
            this->parseColorComponentScalarToken(&a)) {

          *c = Color::FromRGBA(static_cast<uint8_t>(r), static_cast<uint8_t>(g),
                               static_cast<uint8_t>(b), static_cast<uint8_t>(a));
          return true;
        }
        return false;
      },
      c);
}

bool SVGAttributeParser::parseColorToken(Color* c) {
  return this->parseHexColorToken(c) || this->parseNamedColorToken(c) ||
         this->parseRGBAColorToken(c) || this->parseRGBColorToken(c);
}

bool SVGAttributeParser::parseSVGColorType(SVGColorType* color) {
  Color c;
  if (!this->parseColorToken(&c)) {
    return false;
  }
  *color = SVGColorType(c);
  return true;
}

// https://www.w3.org/TR/SVG11/types.html#DataTypeColor
// And https://www.w3.org/TR/CSS2/syndata.html#color-units for the alternative
// forms supported by SVG (e.g. RGB percentages).
template <>
bool SVGAttributeParser::parse(SVGColorType* color) {
  this->parseWSToken();
  if (!this->parseSVGColorType(color)) {
    return false;
  }
  this->parseWSToken();
  return this->parseEOSToken();
}

bool SVGAttributeParser::parseSVGColor(SVGColor* color, SVGColor::Vars&& vars) {
  static const constexpr int kVarsLimit = 32;

  if (SVGColorType c; this->parseSVGColorType(&c)) {
    *color = SVGColor(c, std::move(vars));
    return true;
  }
  if (this->parseExpectedStringToken("currentColor")) {
    *color = SVGColor(SVGColor::Type::CurrentColor, std::move(vars));
    return true;
  }
  // https://drafts.csswg.org/css-variables/#using-variables
  if (this->parseParenthesized(
          "var",
          [this, &vars](SVGColor* colorResult) -> bool {
            std::string ident;
            if (!this->parseIdentToken(&ident) || ident.size() < 2 ||
                ident.compare(0, 2, "--") != 0) {
              return false;
            }
            ident.erase(0, 2);
            vars.push_back(std::move(ident));
            this->parseWSToken();
            if (!this->parseExpectedStringToken(",")) {
              *colorResult = SVGColor(Color::Black(), std::move(vars));
              return true;
            }
            this->parseWSToken();
            if (this->matchStringToken(")")) {
              *colorResult = SVGColor(Color::Black(), std::move(vars));
              return true;
            }
            return vars.size() < kVarsLimit && this->parseSVGColor(colorResult, std::move(vars));
          },
          color)) {
    return true;
  }
  return false;
}

// https://www.w3.org/TR/SVG11/types.html#InterfaceSVGColor
template <>
bool SVGAttributeParser::parse(SVGColor* color) {
  this->parseWSToken();
  if (!this->parseSVGColor(color, SVGColor::Vars())) {
    return false;
  }
  this->parseWSToken();
  return this->parseEOSToken();
}

// https://www.w3.org/TR/SVG11/linking.html#IRIReference
template <>
bool SVGAttributeParser::parse(SVGIRI* iri) {
  // consume preceding whitespace
  this->parseWSToken();

  SVGIRI::Type iriType;
  if (this->parseExpectedStringToken("#")) {
    iriType = SVGIRI::Type::Local;
  } else if (this->matchStringToken("data:")) {
    iriType = SVGIRI::Type::DataURI;
  } else {
    iriType = SVGIRI::Type::Nonlocal;
  }

  const auto start = currentPos;
  if (!this->advanceWhile([](char c) -> bool { return c != ')'; })) {
    return false;
  }
  *iri = SVGIRI(iriType, std::string(start, static_cast<uint32_t>(currentPos - start)));
  return true;
}

// https://www.w3.org/TR/SVG11/types.html#DataTypeFuncIRI
bool SVGAttributeParser::parseFuncIRI(SVGFuncIRI* iri) {
  return this->parseParenthesized(
      "url",
      [this](SVGFuncIRI* iriResult) -> bool {
        SVGIRI iri;
        if (this->parse(&iri)) {
          *iriResult = SVGFuncIRI(std::move(iri));
          return true;
        }
        return false;
      },
      iri);
}

template <>
bool SVGAttributeParser::parse(SVGStringType* result) {
  if (this->parseEOSToken()) {
    return false;
  }
  *result = SVGStringType(currentPos);
  currentPos += result->size();
  return this->parseEOSToken();
}

// https://www.w3.org/TR/SVG11/types.html#DataTypeNumber
template <>
bool SVGAttributeParser::parse(SVGNumberType* number) {
  // consume WS
  this->parseWSToken();

  float s;
  if (this->parseScalarToken(&s)) {
    *number = static_cast<SVGNumberType>(s);
    // consume trailing separators
    this->parseSepToken();
    return true;
  }

  return false;
}

// https://www.w3.org/TR/SVG11/types.html#DataTypeInteger
bool SVGAttributeParser::parseInteger(SVGIntegerType* number) {
  // consume WS
  this->parseWSToken();

  // consume optional '+'
  this->parseExpectedStringToken("+");

  SVGIntegerType i;
  if (this->parseInt32Token(&i)) {
    *number = i;
    // consume trailing separators
    this->parseSepToken();
    return true;
  }

  return false;
}

// https://www.w3.org/TR/SVG11/types.html#DataTypeLength
template <>
bool SVGAttributeParser::parse(SVGLength* length) {
  float s;
  SVGLength::Unit u = SVGLength::Unit::Number;

  if (this->parseScalarToken(&s) &&
      (this->parseLengthUnitToken(u) || this->parseSepToken() || this->parseEOSToken())) {
    *length = SVGLength(s, u);
    // consume trailing separators
    this->parseSepToken();
    return true;
  }

  return false;
}

// https://www.w3.org/TR/SVG11/coords.html#ViewBoxAttribute
bool SVGAttributeParser::parseViewBox(SVGViewBoxType* vb) {
  float x = 0.0f;
  float y = 0.0f;
  float w = 0.0f;
  float h = 0.0f;
  this->parseWSToken();

  bool parsedValue = false;
  if (this->parseScalarToken(&x) && this->parseSepToken() && this->parseScalarToken(&y) &&
      this->parseSepToken() && this->parseScalarToken(&w) && this->parseSepToken() &&
      this->parseScalarToken(&h)) {

    *vb = static_cast<SVGViewBoxType>(Rect::MakeXYWH(x, y, w, h));
    parsedValue = true;
    // consume trailing whitespace
    this->parseWSToken();
  }
  return parsedValue && this->parseEOSToken();
}

template <typename Func, typename T>
bool SVGAttributeParser::parseParenthesized(const char* prefix, Func f, T* result) {
  RestoreCurPos restoreCurPos(this);

  this->parseWSToken();
  if (prefix && !this->parseExpectedStringToken(prefix)) {
    return false;
  }
  this->parseWSToken();
  if (!this->parseExpectedStringToken("(")) {
    return false;
  }
  this->parseWSToken();

  if (!f(result)) {
    return false;
  }

  this->parseWSToken();
  if (!this->parseExpectedStringToken(")")) {
    return false;
  }

  restoreCurPos.clear();
  return true;
}

bool SVGAttributeParser::parseMatrixToken(Matrix* matrix) {
  return this->parseParenthesized(
      "matrix",
      [this](Matrix* m) -> bool {
        float scalars[6];
        for (int i = 0; i < 6; ++i) {
          if (!this->parseScalarToken(scalars + i) || (i <= 4 && !this->parseSepToken())) {
            return false;
          }
        }

        m->setAll(scalars[0], scalars[2], scalars[4], scalars[1], scalars[3], scalars[5]);
        return true;
      },
      matrix);
}

bool SVGAttributeParser::parseTranslateToken(Matrix* matrix) {
  return this->parseParenthesized(
      "translate",
      [this](Matrix* m) -> bool {
        float tx = 0.0;
        float ty = 0.0;
        this->parseWSToken();
        if (!this->parseScalarToken(&tx)) {
          return false;
        }

        if (!this->parseSepToken() || !this->parseScalarToken(&ty)) {
          ty = 0.0;
        }

        m->setTranslate(tx, ty);
        return true;
      },
      matrix);
}

bool SVGAttributeParser::parseScaleToken(Matrix* matrix) {
  return this->parseParenthesized(
      "scale",
      [this](Matrix* m) -> bool {
        float sx = 0.0;
        float sy = 0.0;
        if (!this->parseScalarToken(&sx)) {
          return false;
        }

        if (!(this->parseSepToken() && this->parseScalarToken(&sy))) {
          sy = sx;
        }

        m->setScale(sx, sy);
        return true;
      },
      matrix);
}

bool SVGAttributeParser::parseRotateToken(Matrix* matrix) {
  return this->parseParenthesized(
      "rotate",
      [this](Matrix* m) -> bool {
        float angle;
        if (!this->parseScalarToken(&angle)) {
          return false;
        }

        float cx = 0;
        float cy = 0;
        // optional [<cx> <cy>]
        if (this->parseSepToken() && this->parseScalarToken(&cx)) {
          if (!(this->parseSepToken() && this->parseScalarToken(&cy))) {
            return false;
          }
        }

        m->setRotate(angle, cx, cy);
        return true;
      },
      matrix);
}

bool SVGAttributeParser::parseSkewXToken(Matrix* matrix) {
  return this->parseParenthesized(
      "skewX",
      [this](Matrix* m) -> bool {
        float angle;
        if (!this->parseScalarToken(&angle)) {
          return false;
        }
        m->setSkewX(tanf(DegreesToRadians(angle)));
        return true;
      },
      matrix);
}

bool SVGAttributeParser::parseSkewYToken(Matrix* matrix) {
  return this->parseParenthesized(
      "skewY",
      [this](Matrix* m) -> bool {
        float angle;
        if (!this->parseScalarToken(&angle)) {
          return false;
        }
        m->setSkewY(tanf(DegreesToRadians(angle)));
        return true;
      },
      matrix);
}

// https://www.w3.org/TR/SVG11/coords.html#TransformAttribute
template <>
bool SVGAttributeParser::parse(SVGTransformType* t) {
  Matrix matrix = {};

  bool parsed = false;
  while (true) {
    Matrix m;

    if (!(this->parseMatrixToken(&m) || this->parseTranslateToken(&m) ||
          this->parseScaleToken(&m) || this->parseRotateToken(&m) || this->parseSkewXToken(&m) ||
          this->parseSkewYToken(&m))) {
      break;
    }

    matrix.preConcat(m);
    parsed = true;

    this->parseCommaWspToken();
  }

  this->parseWSToken();
  if (!parsed || !this->parseEOSToken()) {
    return false;
  }

  *t = SVGTransformType(matrix);
  return true;
}

// https://www.w3.org/TR/SVG11/painting.html#SpecifyingPaint
template <>
bool SVGAttributeParser::parse(SVGPaint* paint) {
  SVGColor c;
  SVGFuncIRI iri;
  bool parsedValue = false;

  this->parseWSToken();
  if (this->parseSVGColor(&c, SVGColor::Vars())) {
    *paint = SVGPaint(std::move(c));
    parsedValue = true;
  } else if (this->parseExpectedStringToken("none")) {
    *paint = SVGPaint(SVGPaint::Type::None);
    parsedValue = true;
  } else if (this->parseFuncIRI(&iri)) {
    // optional fallback color
    this->parseWSToken();
    this->parseSVGColor(&c, SVGColor::Vars());
    *paint = SVGPaint(iri.iri(), std::move(c));
    parsedValue = true;
  }
  this->parseWSToken();
  return parsedValue && this->parseEOSToken();
}

// https://www.w3.org/TR/SVG11/masking.html#ClipPathProperty
// https://www.w3.org/TR/SVG11/masking.html#MaskProperty
// https://www.w3.org/TR/SVG11/filters.html#FilterProperty
template <>
bool SVGAttributeParser::parse(SVGFuncIRI* funcIRI) {
  SVGStringType iri;
  bool parsedValue = false;

  if (this->parseExpectedStringToken("none")) {
    *funcIRI = SVGFuncIRI();
    parsedValue = true;
  } else if (this->parseFuncIRI(funcIRI)) {
    parsedValue = true;
  }

  return parsedValue && this->parseEOSToken();
}

// https://www.w3.org/TR/SVG11/painting.html#StrokeLinecapProperty
template <>
bool SVGAttributeParser::parse(SVGLineCap* cap) {
  static const struct {
    SVGLineCap fType;
    const char* fName;
  } gCapInfo[] = {
      {SVGLineCap::Butt, "butt"},
      {SVGLineCap::Round, "round"},
      {SVGLineCap::Square, "square"},
  };

  bool parsedValue = false;
  for (auto i : gCapInfo) {
    if (this->parseExpectedStringToken(i.fName)) {
      *cap = (i.fType);
      parsedValue = true;
      break;
    }
  }

  return parsedValue && this->parseEOSToken();
}

// https://www.w3.org/TR/SVG11/painting.html#StrokeLinejoinProperty
template <>
bool SVGAttributeParser::parse(SVGLineJoin* join) {
  struct JoinInfo {
    SVGLineJoin::Type type;
    const char* name;
  };
  static const std::vector<JoinInfo> joinInfoMap = {
      {SVGLineJoin::Type::Miter, "miter"},
      {SVGLineJoin::Type::Round, "round"},
      {SVGLineJoin::Type::Bevel, "bevel"},
      {SVGLineJoin::Type::Inherit, "inherit"},
  };

  bool parsedValue = false;
  for (auto i : joinInfoMap) {
    if (this->parseExpectedStringToken(i.name)) {
      *join = SVGLineJoin(i.type);
      parsedValue = true;
      break;
    }
  }

  return parsedValue && this->parseEOSToken();
}

// https://www.w3.org/TR/SVG11/coords.html#ObjectBoundingBoxUnits
template <>
bool SVGAttributeParser::parse(SVGObjectBoundingBoxUnits* objectBoundingBoxUnits) {
  bool parsedValue = false;
  if (this->parseExpectedStringToken("userSpaceOnUse")) {
    *objectBoundingBoxUnits =
        SVGObjectBoundingBoxUnits(SVGObjectBoundingBoxUnits::Type::UserSpaceOnUse);
    parsedValue = true;
  } else if (this->parseExpectedStringToken("objectBoundingBox")) {
    *objectBoundingBoxUnits =
        SVGObjectBoundingBoxUnits(SVGObjectBoundingBoxUnits::Type::ObjectBoundingBox);
    parsedValue = true;
  }
  return parsedValue && this->parseEOSToken();
}

// https://www.w3.org/TR/SVG11/shapes.html#PolygonElementPointsAttribute
template <>
bool SVGAttributeParser::parse(SVGPointsType* points) {
  SVGPointsType pts;

  // Skip initial wsp.
  // list-of-points:
  //     wsp* coordinate-pairs? wsp*
  this->advanceWhile(is_ws);

  bool parsedValue = false;
  for (;;) {
    // Adjacent coordinate-pairs separated by comma-wsp.
    // coordinate-pairs:
    //     coordinate-pair
    //     | coordinate-pair comma-wsp coordinate-pairs
    if (parsedValue && !this->parseCommaWspToken()) {
      break;
    }

    float x;
    float y;
    if (!this->parseScalarToken(&x)) {
      break;
    }

    // Coordinate values separated by comma-wsp or '-'.
    // coordinate-pair:
    //     coordinate comma-wsp coordinate
    //     | coordinate negative-coordinate
    if (!this->parseCommaWspToken() && !this->parseEOSToken() && *currentPos != '-') {
      break;
    }

    if (!this->parseScalarToken(&y)) {
      break;
    }

    pts.push_back(Point::Make(x, y));
    parsedValue = true;
  }

  if (parsedValue && this->parseEOSToken()) {
    *points = std::move(pts);
    return true;
  }

  return false;
}

// https://www.w3.org/TR/SVG11/painting.html#FillRuleProperty
template <>
bool SVGAttributeParser::parse(SVGFillRule* fillRule) {
  static const struct {
    SVGFillRule::Type fType;
    const char* fName;
  } gFillRuleInfo[] = {
      {SVGFillRule::Type::NonZero, "nonzero"},
      {SVGFillRule::Type::EvenOdd, "evenodd"},
      {SVGFillRule::Type::Inherit, "inherit"},
  };

  bool parsedValue = false;
  for (auto i : gFillRuleInfo) {
    if (this->parseExpectedStringToken(i.fName)) {
      *fillRule = SVGFillRule(i.fType);
      parsedValue = true;
      break;
    }
  }

  return parsedValue && this->parseEOSToken();
}

// https://www.w3.org/TR/SVG11/painting.html#VisibilityProperty
template <>
bool SVGAttributeParser::parse(SVGVisibility* visibility) {
  static const struct {
    SVGVisibility::Type fType;
    const char* fName;
  } gVisibilityInfo[] = {
      {SVGVisibility::Type::Visible, "visible"},
      {SVGVisibility::Type::Hidden, "hidden"},
      {SVGVisibility::Type::Collapse, "collapse"},
      {SVGVisibility::Type::Inherit, "inherit"},
  };

  bool parsedValue = false;
  for (const auto& parseInfo : gVisibilityInfo) {
    if (this->parseExpectedStringToken(parseInfo.fName)) {
      *visibility = SVGVisibility(parseInfo.fType);
      parsedValue = true;
      break;
    }
  }

  return parsedValue && this->parseEOSToken();
}

// https://www.w3.org/TR/SVG11/painting.html#StrokeDasharrayProperty
template <>
bool SVGAttributeParser::parse(SVGDashArray* dashArray) {
  bool parsedValue = false;
  if (this->parseExpectedStringToken("none")) {
    *dashArray = SVGDashArray(SVGDashArray::Type::None);
    parsedValue = true;
  } else if (this->parseExpectedStringToken("inherit")) {
    *dashArray = SVGDashArray(SVGDashArray::Type::Inherit);
    parsedValue = true;
  } else {
    std::vector<SVGLength> dashes;
    for (;;) {
      SVGLength dash;
      // parseLength() also consumes trailing separators.
      if (!this->parse(&dash)) {
        break;
      }

      dashes.push_back(dash);
      parsedValue = true;
    }

    if (parsedValue) {
      *dashArray = SVGDashArray(std::move(dashes));
    }
  }

  return parsedValue && this->parseEOSToken();
}

// https://www.w3.org/TR/SVG11/text.html#FontFamilyProperty
template <>
bool SVGAttributeParser::parse(SVGFontFamily* family) {
  bool parsedValue = false;
  if (this->parseExpectedStringToken("inherit")) {
    *family = SVGFontFamily();
    parsedValue = true;
  } else {
    // The spec allows specifying a comma-separated list for explicit fallback order.
    // For now, we only use the first entry and rely on the font manager to handle fallback.
    const auto comma = strchr(currentPos, ',');
    auto family_name = comma ? std::string(currentPos, static_cast<uint32_t>(comma - currentPos))
                             : std::string(currentPos);
    *family = SVGFontFamily(family_name);
    currentPos += strlen(currentPos);
    parsedValue = true;
  }

  return parsedValue && this->parseEOSToken();
}

// https://www.w3.org/TR/SVG11/text.html#FontSizeProperty
template <>
bool SVGAttributeParser::parse(SVGFontSize* size) {
  bool parsedValue = false;
  if (this->parseExpectedStringToken("inherit")) {
    *size = SVGFontSize();
    parsedValue = true;
  } else {
    SVGLength length;
    if (this->parse(&length)) {
      *size = SVGFontSize(length);
      parsedValue = true;
    }
  }

  return parsedValue && this->parseEOSToken();
}

// https://www.w3.org/TR/SVG11/text.html#FontStyleProperty
template <>
bool SVGAttributeParser::parse(SVGFontStyle* style) {
  static constexpr std::tuple<const char*, SVGFontStyle::Type> styleMap[] = {
      {"normal", SVGFontStyle::Type::Normal},
      {"italic", SVGFontStyle::Type::Italic},
      {"oblique", SVGFontStyle::Type::Oblique},
      {"inherit", SVGFontStyle::Type::Inherit},
  };

  bool parsedValue = false;
  SVGFontStyle::Type type;

  if (this->parseEnumMap(styleMap, &type)) {
    *style = SVGFontStyle(type);
    parsedValue = true;
  }

  return parsedValue && this->parseEOSToken();
}

// https://www.w3.org/TR/SVG11/text.html#FontWeightProperty
template <>
bool SVGAttributeParser::parse(SVGFontWeight* weight) {
  static constexpr std::tuple<const char*, SVGFontWeight::Type> weightMap[] = {
      {"normal", SVGFontWeight::Type::Normal}, {"bold", SVGFontWeight::Type::Bold},
      {"bolder", SVGFontWeight::Type::Bolder}, {"lighter", SVGFontWeight::Type::Lighter},
      {"100", SVGFontWeight::Type::W100},      {"200", SVGFontWeight::Type::W200},
      {"300", SVGFontWeight::Type::W300},      {"400", SVGFontWeight::Type::W400},
      {"500", SVGFontWeight::Type::W500},      {"600", SVGFontWeight::Type::W600},
      {"700", SVGFontWeight::Type::W700},      {"800", SVGFontWeight::Type::W800},
      {"900", SVGFontWeight::Type::W900},      {"inherit", SVGFontWeight::Type::Inherit},
  };

  bool parsedValue = false;
  SVGFontWeight::Type type;

  if (this->parseEnumMap(weightMap, &type)) {
    *weight = SVGFontWeight(type);
    parsedValue = true;
  }

  return parsedValue && this->parseEOSToken();
}

// https://www.w3.org/TR/SVG11/text.html#TextAnchorProperty
template <>
bool SVGAttributeParser::parse(SVGTextAnchor* anchor) {
  static constexpr std::tuple<const char*, SVGTextAnchor::Type> anchorMap[] = {
      {"start", SVGTextAnchor::Type::Start},
      {"middle", SVGTextAnchor::Type::Middle},
      {"end", SVGTextAnchor::Type::End},
      {"inherit", SVGTextAnchor::Type::Inherit},
  };

  bool parsedValue = false;
  SVGTextAnchor::Type type;

  if (this->parseEnumMap(anchorMap, &type)) {
    *anchor = SVGTextAnchor(type);
    parsedValue = true;
  }

  return parsedValue && this->parseEOSToken();
}

// https://www.w3.org/TR/SVG11/coords.html#PreserveAspectRatioAttribute
bool SVGAttributeParser::parsePreserveAspectRatio(SVGPreserveAspectRatio* par) {
  static constexpr std::tuple<const char*, SVGPreserveAspectRatio::Align> alignMap[] = {
      {"none", SVGPreserveAspectRatio::Align::None},
      {"xMinYMin", SVGPreserveAspectRatio::Align::XMinYMin},
      {"xMidYMin", SVGPreserveAspectRatio::Align::XMidYMin},
      {"xMaxYMin", SVGPreserveAspectRatio::Align::XMaxYMin},
      {"xMinYMid", SVGPreserveAspectRatio::Align::XMinYMid},
      {"xMidYMid", SVGPreserveAspectRatio::Align::XMidYMid},
      {"xMaxYMid", SVGPreserveAspectRatio::Align::XMaxYMid},
      {"xMinYMax", SVGPreserveAspectRatio::Align::XMinYMax},
      {"xMidYMax", SVGPreserveAspectRatio::Align::XMidYMax},
      {"xMaxYMax", SVGPreserveAspectRatio::Align::XMaxYMax},
  };

  static constexpr std::tuple<const char*, SVGPreserveAspectRatio::Scale> scaleMap[] = {
      {"meet", SVGPreserveAspectRatio::Scale::Meet},
      {"slice", SVGPreserveAspectRatio::Scale::Slice},
  };

  bool parsedValue = false;

  // ignoring optional 'defer'
  this->parseExpectedStringToken("defer");
  this->parseWSToken();

  if (this->parseEnumMap(alignMap, &par->align)) {
    parsedValue = true;

    // optional scaling selector
    this->parseWSToken();
    this->parseEnumMap(scaleMap, &par->scale);
  }

  return parsedValue && this->parseEOSToken();
}

template <>
bool SVGAttributeParser::parse(SVGPreserveAspectRatio* par) {
  return this->parsePreserveAspectRatio(par);
}

// https://www.w3.org/TR/SVG11/types.html#DataTypeCoordinates
template <typename T>
bool SVGAttributeParser::parseList(std::vector<T>* vals) {
  ASSERT(vals->empty());

  T v;
  for (;;) {
    if (!this->parse(&v)) {
      break;
    }

    vals->push_back(v);

    this->parseCommaWspToken();
  }

  return !vals->empty() && this->parseEOSToken();
}

template <>
bool SVGAttributeParser::parse(std::vector<SVGLength>* lengths) {
  return this->parseList(lengths);
}

template <>
bool SVGAttributeParser::parse(std::vector<SVGNumberType>* numbers) {
  return this->parseList(numbers);
}

template <>
bool SVGAttributeParser::parse(SVGColorspace* colorspace) {
  static constexpr std::tuple<const char*, SVGColorspace> colorspaceMap[] = {
      {"auto", SVGColorspace::Auto},
      {"sRGB", SVGColorspace::SRGB},
      {"linearRGB", SVGColorspace::LinearRGB},
  };

  return this->parseEnumMap(colorspaceMap, colorspace) && this->parseEOSToken();
}

// https://www.w3.org/TR/SVG11/painting.html#DisplayProperty
template <>
bool SVGAttributeParser::parse(SVGDisplay* display) {
  static const struct {
    SVGDisplay fType;
    const char* fName;
  } gDisplayInfo[] = {
      {SVGDisplay::Inline, "inline"},
      {SVGDisplay::None, "none"},
  };

  bool parsedValue = false;
  for (const auto& parseInfo : gDisplayInfo) {
    if (this->parseExpectedStringToken(parseInfo.fName)) {
      *display = parseInfo.fType;
      parsedValue = true;
      break;
    }
  }

  return parsedValue && this->parseEOSToken();
}

template <>
bool SVGAttributeParser::parse(SVGMaskType* maskType) {
  static constexpr std::tuple<const char*, SVGMaskType::Type> typeMap[] = {
      {"luminance", SVGMaskType::Type::Luminance},
      {"alpha", SVGMaskType::Type::Alpha},
  };

  bool parsedValue = false;
  SVGMaskType::Type type;

  if (this->parseEnumMap(typeMap, &type)) {
    *maskType = SVGMaskType(type);
    parsedValue = true;
  }

  return parsedValue && this->parseEOSToken();
}

}  // namespace tgfx
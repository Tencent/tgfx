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

#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>
#include "tgfx/core/Color.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Typeface.h"
#include "tgfx/svg/SVGTypes.h"

namespace tgfx {

class SVGAttributeParser {
 public:
  explicit SVGAttributeParser(const char* str, size_t length);

  bool parseInteger(SVGIntegerType*);
  bool parseViewBox(SVGViewBoxType*);
  bool parsePreserveAspectRatio(SVGPreserveAspectRatio*);

  bool parse(SVGIntegerType* v) {
    return parseInteger(v);
  }

  template <typename T>
  using ParseResult = std::optional<T>;

  template <typename T>
  static ParseResult<T> parse(const std::string& value) {
    ParseResult<T> result;
    T parsedValue;
    if (SVGAttributeParser(value.c_str(), value.length()).parse(&parsedValue)) {
      result = parsedValue;
    }
    return result;
  }

  template <typename T>
  static ParseResult<T> parse(const std::string& expectedName, const std::string& name,
                              const std::string& value) {
    if (name == expectedName) {
      return parse<T>(value);
    }

    return ParseResult<T>();
  }

  template <typename PropertyT>
  static ParseResult<PropertyT> parseProperty(const std::string& expectedName,
                                              const std::string& name, const std::string& value) {
    if (name != expectedName) {
      return ParseResult<PropertyT>();
    }

    if (value == "inherit") {
      PropertyT result(SVGPropertyState::Inherit);
      return ParseResult<PropertyT>(result);
    }

    auto pr = parse<typename PropertyT::ValueT>(value);
    if (pr.has_value()) {
      PropertyT result(*pr);
      return ParseResult<PropertyT>(result);
    }

    return ParseResult<PropertyT>();
  }

  // Stack-only
  void* operator new(size_t) = delete;
  void* operator new(size_t, void*) = delete;

 private:
  class RestoreCurPos {
   public:
    explicit RestoreCurPos(SVGAttributeParser* self) : self(self), currentPos(self->currentPos) {
    }

    ~RestoreCurPos() {
      if (self) {
        self->currentPos = this->currentPos;
      }
    }

    void clear() {
      self = nullptr;
    }

   private:
    SVGAttributeParser* self;
    const char* currentPos;

   public:
    RestoreCurPos(const RestoreCurPos&) = delete;
    RestoreCurPos& operator=(const RestoreCurPos&) = delete;
  };

  template <typename T>
  bool parse(T*);

  template <typename F>
  bool advanceWhile(F func);

  bool matchStringToken(const char* token, const char** newPos = nullptr) const;
  bool matchHexToken(const char** newPos) const;

  bool parseWSToken();
  bool parseEOSToken();
  bool parseSepToken();
  bool parseCommaWspToken();
  bool parseExpectedStringToken(const char*);
  bool parseScalarToken(float*);
  bool parseInt32Token(int32_t*);
  bool parseEscape(Unichar*);
  bool parseIdentToken(std::string*);
  bool parseLengthUnitToken(SVGLength::Unit&);
  bool parseNamedColorToken(Color*);
  bool parseHexColorToken(Color*);
  bool parseColorComponentScalarToken(int32_t*);
  bool parseColorComponentIntegralToken(int32_t*);
  bool parseColorComponentFractionalToken(int32_t*);
  bool parseColorComponentToken(int32_t*);
  bool parseColorToken(Color*);
  bool parseRGBColorToken(Color*);
  bool parseRGBAColorToken(Color*);
  bool parseColorFunctionToken(Color*);
  bool parseSVGColor(SVGColor*, SVGColor::Vars&&);
  bool parseSVGColorType(SVGColorType*);
  bool parseFuncIRI(SVGFuncIRI*);

  // Transform helpers
  bool parseMatrixToken(Matrix*);
  bool parseTranslateToken(Matrix*);
  bool parseScaleToken(Matrix*);
  bool parseRotateToken(Matrix*);
  bool parseSkewXToken(Matrix*);
  bool parseSkewYToken(Matrix*);

  // Parses a sequence of 'WS* <prefix> WS* (<nested>)', where the nested sequence
  // is handled by the passed functor.
  template <typename Func, typename T>
  bool parseParenthesized(const char* prefix, Func, T* result);

  template <typename T>
  bool parseList(std::vector<T>*);

  template <typename T, typename TArray>
  bool parseEnumMap(const TArray& arr, T* result) {
    for (size_t i = 0; i < std::size(arr); ++i) {
      if (this->parseExpectedStringToken(std::get<0>(arr[i]))) {
        *result = std::get<1>(arr[i]);
        return true;
      }
    }
    return false;
  }

  // The current position in the input string.
  const char* currentPos;
  const char* endPos;
};
}  // namespace tgfx

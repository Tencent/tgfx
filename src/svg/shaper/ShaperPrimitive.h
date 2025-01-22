/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "tgfx/svg/shaper/Shaper.h"

namespace tgfx {

template <typename RunIteratorSubclass>
class TrivialRunIterator : public RunIteratorSubclass {
 public:
  explicit TrivialRunIterator(size_t utf8Bytes) : end(utf8Bytes), _atEnd(end == 0) {
  }
  void consume() override {
    _atEnd = true;
  }
  size_t endOfCurrentRun() const override {
    return _atEnd ? end : 0;
  }
  bool atEnd() const override {
    return _atEnd;
  }

 private:
  size_t end;
  bool _atEnd;
};

class TrivialFontRunIterator : public TrivialRunIterator<FontRunIterator> {
 public:
  TrivialFontRunIterator(Font font, size_t utf8Bytes)
      : TrivialRunIterator(utf8Bytes), font(std::move(font)) {
  }
  const Font& currentFont() const override {
    return font;
  }

 private:
  Font font;
};

class TrivialBiDiRunIterator : public TrivialRunIterator<BiDiRunIterator> {
 public:
  TrivialBiDiRunIterator(uint8_t bidiLevel, size_t utf8Bytes)
      : TrivialRunIterator(utf8Bytes), bidiLevel(bidiLevel) {
  }
  uint8_t currentLevel() const override {
    return bidiLevel;
  }

 private:
  uint8_t bidiLevel;
};

class TrivialScriptRunIterator : public TrivialRunIterator<ScriptRunIterator> {
 public:
  TrivialScriptRunIterator(FourByteTag script, size_t utf8Bytes)
      : TrivialRunIterator(utf8Bytes), script(script) {
  }
  FourByteTag currentScript() const override {
    return script;
  }

 private:
  FourByteTag script;
};

class TrivialLanguageRunIterator : public TrivialRunIterator<LanguageRunIterator> {
 public:
  TrivialLanguageRunIterator(const char* language, size_t utf8Bytes)
      : TrivialRunIterator(utf8Bytes), language(language) {
  }

  std::string currentLanguage() const override {
    return language;
  }

 private:
  std::string language;
};

class ShaperPrimitive : public Shaper {
 public:
  ShaperPrimitive() = default;

 private:
  void shape(const char* utf8, size_t utf8Bytes, FontRunIterator& fontIter,
             BiDiRunIterator& bidiIter, ScriptRunIterator& scriptIter,
             LanguageRunIterator& langIter, const RunFeature* features, size_t featuresSize,
             float width, RunHandler* handler) const override;
};

}  // namespace tgfx
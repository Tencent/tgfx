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

#include <memory>
#include <string_view>
#include <utility>
#include "tgfx/core/Font.h"
#include "tgfx/core/FontManager.h"
#include "tgfx/core/Point.h"
#include "tgfx/core/Shape.h"
#include "tgfx/core/Typeface.h"
#include "tgfx/svg/SVGTypes.h"

namespace tgfx {

/**
 * ISO 15924, Codes for the representation of names of scripts. 
 * https://en.wikipedia.org/wiki/ISO_15924
 */
using FourByteTag = uint32_t;
static inline constexpr FourByteTag SetFourByteTag(char a, char b, char c, char d) {
  return ((static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(b) << 16) |
          (static_cast<uint32_t>(c) << 8) | static_cast<uint32_t>(d));
}

class RunIterator {
 public:
  virtual ~RunIterator() = default;
  /** Set state to that of current run and move iterator to end of that run. */
  virtual void consume() = 0;
  /** Offset to one past the last (utf8) element in the current run. */
  virtual size_t endOfCurrentRun() const = 0;
  /** Return true if consume should no longer be called. */
  virtual bool atEnd() const = 0;
};

class FontRunIterator : public RunIterator {
 public:
  virtual const Font& currentFont() const = 0;
};

class BiDiRunIterator : public RunIterator {
 public:
  /** The unicode bidi embedding level (even ltr, odd rtl) */
  virtual uint8_t currentLevel() const = 0;
};

class ScriptRunIterator : public RunIterator {
 public:
  // Should be iso15924 codes.
  virtual FourByteTag currentScript() const = 0;
};

class LanguageRunIterator : public RunIterator {
 public:
  /** Should be BCP-47, c locale names may also work. */
  virtual std::string currentLanguage() const = 0;
};

struct RunFeature {
  FourByteTag tag;
  uint32_t value;
  size_t start;  // Offset to the start (utf8) element of the run.
  size_t end;    // Offset to one past the last (utf8) element of the run.
};

class RunHandler {
 public:
  virtual ~RunHandler() = default;

  struct Range {
    constexpr Range() : _begin(0), _size(0) {
    }
    constexpr Range(size_t begin, size_t size) : _begin(begin), _size(size) {
    }

    constexpr size_t begin() const {
      return _begin;
    }
    constexpr size_t end() const {
      return begin() + size();
    }
    constexpr size_t size() const {
      return _size;
    }

   private:
    size_t _begin;
    size_t _size;
  };

  struct RunInfo {
    const Font& font;
    uint8_t bidiLevel;
    Point advance;
    size_t glyphCount;
    Range utf8Range;
  };

  struct Buffer {
    GlyphID* glyphs;     // required
    Point* positions;    // required, if (!offsets) put glyphs[i] at positions[i]
                         //           if ( offsets) positions[i+1]-positions[i] are advances
    Point* offsets;      // optional, if ( offsets) put glyphs[i] at positions[i]+offsets[i]
    uint32_t* clusters;  // optional, utf8+clusters[i] starts run which produced glyphs[i]
    Point point;         // offset to add to all positions
  };

  /** Called when beginning a line. */
  virtual void beginLine() = 0;

  /** Called once for each run in a line. Can compute baselines and offsets. */
  virtual void runInfo(const RunInfo&) = 0;

  /** Called after all runInfo calls for a line. */
  virtual void commitRunInfo() = 0;

  /** Called for each run in a line after commitRunInfo. The buffer will be filled out. */
  virtual Buffer runBuffer(const RunInfo&) = 0;

  /** Called after each runBuffer is filled out. */
  virtual void commitRunBuffer(const RunInfo&) = 0;

  /** Called when ending a line. */
  virtual void commitLine() = 0;
};

class Shaper {
 public:
  Shaper() = default;
  virtual ~Shaper() = default;
  Shaper(const Shaper&) = delete;
  Shaper& operator=(const Shaper&) = delete;

  virtual void shape(const char* utf8, size_t utf8Bytes, FontRunIterator& fontIter,
                     BiDiRunIterator& bidiIter, ScriptRunIterator& scriptIter,
                     LanguageRunIterator& langIter, const RunFeature* features, size_t featuresSize,
                     float width, RunHandler* handler) const = 0;

  static std::unique_ptr<FontRunIterator> MakeFontMgrRunIterator(
      const char* utf8, size_t utf8Bytes, const Font& font, std::shared_ptr<FontManager> fallback);

  static std::unique_ptr<LanguageRunIterator> MakeStdLanguageRunIterator(const char* utf8,
                                                                         size_t utf8Bytes);
};

}  // namespace tgfx
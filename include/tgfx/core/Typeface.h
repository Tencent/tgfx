/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include <mutex>
#include <unordered_map>
#include "tgfx/core/Data.h"

namespace tgfx {
/**
 * 16 bit unsigned integer to hold a glyph index.
 */
typedef uint16_t GlyphID;

/**
 * 32-bit signed integer to hold a UTF-32 code unit.
 */
typedef int32_t Unichar;

typedef uint32_t FontTableTag;

class ScalerContext;

/**
 * A set of character glyphs and layout information for drawing text.
 */
class Typeface {
 public:
  /**
   * Returns a non-null typeface which contains no glyphs.
   */
  static std::shared_ptr<Typeface> MakeEmpty();

  /**
   * Returns a typeface reference for the given font family and font style. If the family and
   * style cannot be matched identically, the best match is found and returned, which is never
   * nullptr.
   */
  static std::shared_ptr<Typeface> MakeFromName(const std::string& fontFamily,
                                                const std::string& fontStyle);

  /**
   * Creates a new typeface for the given file path and ttc index. Returns nullptr if the typeface
   * can't be created.
   */
  static std::shared_ptr<Typeface> MakeFromPath(const std::string& fontPath, int ttcIndex = 0);

  /**
   * Creates a new typeface for the given file bytes and ttc index. Returns nullptr if the typeface
   * can't be created.
   */
  static std::shared_ptr<Typeface> MakeFromBytes(const void* bytes, size_t length,
                                                 int ttcIndex = 0);

  /**
   * Creates a new typeface for the given file data and ttc index. Returns nullptr if the typeface
   * can't be created.
   */
  static std::shared_ptr<Typeface> MakeFromData(std::shared_ptr<Data> data, int ttcIndex = 0);

  virtual ~Typeface() = default;

  /**
   * Returns the uniqueID for the specified typeface.
   */
  virtual uint32_t uniqueID() const = 0;

  /**
   * Returns the family name of this typeface.
   */
  virtual std::string fontFamily() const = 0;

  /**
   * Returns the style name of this typeface.
   */
  virtual std::string fontStyle() const = 0;

  /**
   *  Return the number of glyphs in this typeface.
   */
  virtual size_t glyphsCount() const = 0;

  /**
   * Returns the number of glyph space units per em for this typeface.
   */
  virtual int unitsPerEm() const = 0;

  /**
   * Returns true if this typeface has color glyphs, for example, color emojis.
   */
  virtual bool hasColor() const = 0;

  /**
   * Returns true if this typeface has outline glyphs, meaning it can generate paths.
   */
  virtual bool hasOutlines() const = 0;

  /**
   * Returns the glyph ID corresponds to the specified glyph name. The glyph name must be in utf-8
   * encoding. Returns 0 if the glyph name is not in this typeface.
   */
  GlyphID getGlyphID(const std::string& name) const;

  /**
   * Returns the glyph ID corresponds to the specified unicode code point. Returns 0 if the code
   * point is not in this typeface.
   */
  virtual GlyphID getGlyphID(Unichar unichar) const = 0;

  virtual std::shared_ptr<Data> getBytes() const = 0;

  /**
   * Returns an immutable copy of the requested font table, or nullptr if that table was not found.
   */
  virtual std::shared_ptr<Data> copyTableData(FontTableTag tag) const = 0;

 protected:
  mutable std::mutex locker = {};

 private:
  std::unordered_map<float, std::weak_ptr<ScalerContext>> scalerContexts = {};

  friend class ScalerContext;
};
}  // namespace tgfx

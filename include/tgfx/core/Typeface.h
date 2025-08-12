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

#include <mutex>
#include <unordered_map>
#include <vector>
#include "tgfx/core/Data.h"
#include "tgfx/core/FontStyle.h"
#include "tgfx/core/Rect.h"

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
static inline constexpr FontTableTag SetFourByteTag(char a, char b, char c, char d) {
  return ((static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(b) << 16) |
          (static_cast<uint32_t>(c) << 8) | static_cast<uint32_t>(d));
}

class ScalerContext;
class AdvancedTypefaceProperty;

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
   * Returns a typeface object for the specified font family and style name. If an exact match is not
   * found, the closest match is returned, or nullptr if no match is found.
   */
  static std::shared_ptr<Typeface> MakeFromName(const std::string& fontFamily,
                                                const std::string& fontStyle);

  /**
   * Returns a typeface object for the specified font family and FontStyle object. If an exact match
   * is not found, the closest match is returned, or nullptr if no match is found.
   */
  static std::shared_ptr<Typeface> MakeFromName(const std::string& fontFamily,
                                                const FontStyle& fontStyle);

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

  /**
   * Returns a rectangle that represents the union of the bounds of all the glyphs, but each one
   * positioned at (0,0). This may be conservatively large, and will not take into account any
   * hitting or other size-specific adjustments.
   */
  Rect getBounds() const;

  size_t getTableSize(FontTableTag tag) const;

 protected:
  /**
   * Gets the mapping from GlyphID to unicode. The array index is GlyphID, and the array value is
   * unicode. The array length is glyphsCount(). 
   * This method is only implemented when compiling the SVG or PDF export module.
   */
  virtual std::vector<Unichar> getGlyphToUnicodeMap() const;

  virtual std::shared_ptr<ScalerContext> onCreateScalerContext(float size) const = 0;

  /**
   * Returns a Data object containing the font data, or nullptr if unavailable.
   * For local file fonts, this reads the file and returns its byte data.
   */
  virtual std::shared_ptr<Data> openAndGetBytes() const = 0;

  /**
   * Returns advanced properties of the typeface. This method is used by the PDF backend.
   */
  virtual AdvancedTypefaceProperty getAdvancedProperty() const;

  mutable std::mutex locker = {};

  std::weak_ptr<Typeface> weakThis;

 private:
  /**
   *  Returns a ScalerContext for the given size.
   */
  std::shared_ptr<ScalerContext> getScalerContext(float size);

  virtual bool isCustom() const;

  virtual bool onComputeBounds(Rect* bounds) const;

  std::unordered_map<float, std::weak_ptr<ScalerContext>> scalerContexts = {};
  mutable Rect bounds = {};
  mutable std::once_flag onceFlag = {};

  friend class Font;
  friend class ScalerContext;
  friend class FTScalerContext;
  friend class GlyphConverter;
  friend class CGMask;
  friend class WebMask;
  friend class SVGExportContext;
  friend class RenderContext;
  friend class PDFExportContext;
  friend class PDFFont;
};
}  // namespace tgfx

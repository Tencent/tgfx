/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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
#include <vector>
#include "tgfx/core/Color.h"
#include "tgfx/core/Font.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Shape.h"
#include "tgfx/core/TextBlob.h"

namespace tgfx {

/**
 * GlyphStyle holds the style properties for text rendering.
 * The alpha channel of colors serves as the blend factor.
 * A blend factor of 0 means no override (use original style), 1 means full override.
 */
struct GlyphStyle {
  Color fillColor = Color::Transparent();
  Color strokeColor = Color::Transparent();
  float strokeWidth = 0.0f;
  float strokeWidthFactor = 0.0f;
  float alpha = 1.0f;

  bool operator==(const GlyphStyle& other) const;
  bool operator!=(const GlyphStyle& other) const;
};

/**
 * Glyph stores information for a single glyph with its transformation and style overrides.
 */
struct Glyph {
  GlyphID glyphID = 0;
  Font font = {};
  Point anchor = Point::Zero();  // offset relative to default anchor (advance*0.5, 0)
  Matrix matrix = Matrix::I();
  GlyphStyle style = {};
};

/**
 * StyledGlyphRun represents a group of consecutive glyphs with the same style.
 */
struct StyledGlyphRun {
  std::shared_ptr<TextBlob> textBlob = nullptr;
  Matrix matrix = Matrix::I();
  GlyphStyle style = {};
};

/**
 * Geometry represents a drawable element in VectorLayer. It can hold a Shape, TextBlob, or
 * individual Glyphs. The content is lazily converted between formats as needed.
 */
class Geometry {
 public:
  struct TextBlobResult {
    std::shared_ptr<TextBlob> blob = nullptr;
    Matrix commonMatrix = Matrix::I();
  };

  /**
   * Creates a copy of this geometry including all fields.
   */
  std::unique_ptr<Geometry> clone() const;

  /**
   * Returns the Shape representation. Converts from TextBlob if needed and caches the result.
   * If glyphs have a common transformation matrix, it will be extracted and combined with the
   * geometry's matrix field.
   */
  std::shared_ptr<Shape> getShape();

  /**
   * Returns glyph runs grouped by consecutive glyphs with the same style.
   * Each run contains a TextBlob with optimized positioning and a shared matrix for common
   * transformations. If no glyphs exist but textBlob is present, returns a single run with the
   * original textBlob.
   */
  const std::vector<StyledGlyphRun>& getGlyphRuns();

  /**
   * Returns true if this geometry contains text content (textBlob or glyphs).
   */
  bool hasText() const {
    return textBlob != nullptr || !glyphs.empty();
  }

  /**
   * The transformation matrix applied to this geometry.
   */
  Matrix matrix = Matrix::I();

  /**
   * The shape content, used for path-based geometries.
   */
  std::shared_ptr<Shape> shape = nullptr;

  /**
   * The text blob content, used for text-based geometries.
   */
  std::shared_ptr<TextBlob> textBlob = nullptr;

  /**
   * Individual glyphs with per-glyph transformations, expanded from textBlob for modification.
   */
  std::vector<Glyph> glyphs = {};

  /**
   * Anchor offsets for each glyph, applied when expanding to glyphs.
   */
  std::vector<Point> anchors = {};

 private:
  friend class VectorContext;

  void expandToGlyphs();
  void convertToShape();

  TextBlobResult buildTextBlob(const std::vector<Glyph>& inputGlyphs);
  void buildGlyphRuns();

  std::vector<StyledGlyphRun> glyphRuns = {};
};

}  // namespace tgfx

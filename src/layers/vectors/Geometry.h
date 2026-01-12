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
#include "tgfx/core/Font.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Shape.h"
#include "tgfx/core/TextBlob.h"

namespace tgfx {

/**
 * Glyph stores information for a single glyph with its transformation.
 */
struct Glyph {
  GlyphID glyphID = 0;
  Font font = {};
  Matrix matrix = Matrix::I();
};

/**
 * Geometry represents a drawable element in VectorLayer. It can hold a Shape, TextBlob, or
 * individual Glyphs. The content is lazily converted between formats as needed.
 */
class Geometry {
 public:
  /**
   * Creates a copy of this geometry including all fields.
   */
  std::unique_ptr<Geometry> clone() const;

  /**
   * Returns the TextBlob. If glyphs are present, rebuilds and caches TextBlob from them.
   * Returns nullptr if this is a pure shape geometry.
   */
  std::shared_ptr<TextBlob> getTextBlob();

  /**
   * Returns the Shape representation. Converts from TextBlob if needed and caches the result.
   */
  std::shared_ptr<Shape> getShape();

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

 private:
  friend class VectorContext;

  void expandToGlyphs();
  void convertToShape();
  std::shared_ptr<TextBlob> buildTextBlob();
};

}  // namespace tgfx

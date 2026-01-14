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

#include "Geometry.h"
#include "core/GlyphRunList.h"
#include "core/utils/Log.h"
#include "tgfx/core/TextBlobBuilder.h"

namespace tgfx {

bool GlyphStyle::operator==(const GlyphStyle& other) const {
  return fillColor == other.fillColor && strokeColor == other.strokeColor &&
         strokeWidth == other.strokeWidth && strokeWidthFactor == other.strokeWidthFactor &&
         alpha == other.alpha;
}

bool GlyphStyle::operator!=(const GlyphStyle& other) const {
  return !(*this == other);
}

std::unique_ptr<Geometry> Geometry::clone() const {
  auto cloned = std::make_unique<Geometry>();
  cloned->matrix = matrix;
  cloned->shape = shape;
  cloned->textBlob = textBlob;
  cloned->glyphs = glyphs;
  return cloned;
}

std::shared_ptr<Shape> Geometry::getShape() {
  if (shape == nullptr) {
    auto blob = textBlob;
    if (blob == nullptr && !glyphs.empty()) {
      blob = buildTextBlob();
    }
    if (blob != nullptr) {
      shape = Shape::MakeFrom(blob);
    }
  }
  return shape;
}

const std::vector<StyledGlyphRun>& Geometry::getGlyphRuns() {
  if (glyphRuns.empty()) {
    buildGlyphRuns();
  }
  return glyphRuns;
}

void Geometry::convertToShape() {
  DEBUG_ASSERT(hasText());
  getShape();
  textBlob = nullptr;
  glyphs.clear();
}

void Geometry::expandToGlyphs() {
  DEBUG_ASSERT(textBlob != nullptr);
  size_t totalCount = 0;
  for (const auto& run : GlyphRunList(textBlob.get())) {
    totalCount += run.runSize();
  }
  glyphs.clear();
  glyphs.reserve(totalCount);
  for (const auto& run : GlyphRunList(textBlob.get())) {
    for (size_t i = 0; i < run.runSize(); i++) {
      Glyph glyph = {};
      glyph.glyphID = run.glyphs[i];
      glyph.font = run.font;
      glyph.matrix = run.getMatrix(i);
      glyphs.push_back(glyph);
    }
  }
  textBlob = nullptr;
  shape = nullptr;
}

static void FlushGlyphRun(TextBlobBuilder* builder, const Font& font,
                          std::vector<GlyphID>* glyphIDs, std::vector<Matrix>* matrices) {
  if (glyphIDs->empty()) {
    return;
  }
  const auto& buffer = builder->allocRunMatrix(font, glyphIDs->size());
  memcpy(buffer.glyphs, glyphIDs->data(), glyphIDs->size() * sizeof(GlyphID));
  for (size_t i = 0; i < matrices->size(); i++) {
    auto* p = buffer.positions + i * 6;
    p[0] = (*matrices)[i].getScaleX();
    p[1] = (*matrices)[i].getSkewX();
    p[2] = (*matrices)[i].getTranslateX();
    p[3] = (*matrices)[i].getSkewY();
    p[4] = (*matrices)[i].getScaleY();
    p[5] = (*matrices)[i].getTranslateY();
  }
  glyphIDs->clear();
  matrices->clear();
}

std::shared_ptr<TextBlob> Geometry::buildTextBlob() {
  if (glyphs.empty()) {
    return nullptr;
  }
  TextBlobBuilder builder;
  auto currentFont = glyphs[0].font;
  std::vector<GlyphID> glyphIDs = {};
  std::vector<Matrix> glyphMatrices = {};
  glyphIDs.reserve(glyphs.size());
  glyphMatrices.reserve(glyphs.size());

  for (const auto& glyph : glyphs) {
    if (glyph.font != currentFont) {
      FlushGlyphRun(&builder, currentFont, &glyphIDs, &glyphMatrices);
      currentFont = glyph.font;
    }
    glyphIDs.push_back(glyph.glyphID);
    glyphMatrices.push_back(glyph.matrix);
  }
  FlushGlyphRun(&builder, currentFont, &glyphIDs, &glyphMatrices);

  return builder.build();
}

void Geometry::buildGlyphRuns() {
  glyphRuns.clear();
  if (glyphs.empty()) {
    if (textBlob) {
      glyphRuns.push_back({textBlob, {}});
    }
    return;
  }

  auto builder = std::make_unique<TextBlobBuilder>();
  auto currentFont = glyphs[0].font;
  auto currentStyle = glyphs[0].style;
  std::vector<GlyphID> glyphIDs = {};
  std::vector<Matrix> glyphMatrices = {};
  glyphIDs.reserve(glyphs.size());
  glyphMatrices.reserve(glyphs.size());

  for (const auto& glyph : glyphs) {
    if (glyph.style != currentStyle) {
      FlushGlyphRun(builder.get(), currentFont, &glyphIDs, &glyphMatrices);
      auto blob = builder->build();
      if (blob) {
        glyphRuns.push_back({std::move(blob), currentStyle});
      }
      builder = std::make_unique<TextBlobBuilder>();
      currentFont = glyph.font;
      currentStyle = glyph.style;
    } else if (glyph.font != currentFont) {
      FlushGlyphRun(builder.get(), currentFont, &glyphIDs, &glyphMatrices);
      currentFont = glyph.font;
    }
    glyphIDs.push_back(glyph.glyphID);
    glyphMatrices.push_back(glyph.matrix);
  }

  FlushGlyphRun(builder.get(), currentFont, &glyphIDs, &glyphMatrices);
  auto blob = builder->build();
  if (blob) {
    glyphRuns.push_back({std::move(blob), currentStyle});
  }
}

}  // namespace tgfx

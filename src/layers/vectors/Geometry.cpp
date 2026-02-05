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
#include "core/GlyphTransform.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "tgfx/core/RSXform.h"
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
  cloned->anchors = anchors;
  return cloned;
}

std::shared_ptr<Shape> Geometry::getShape() {
  if (shape == nullptr) {
    auto blob = textBlob;
    Matrix blobMatrix = Matrix::I();
    if (blob == nullptr && !glyphs.empty()) {
      auto result = buildTextBlob(glyphs);
      blob = result.blob;
      blobMatrix = result.commonMatrix;
    }
    if (blob != nullptr) {
      shape = Shape::MakeFrom(blob);
      if (!blobMatrix.isIdentity()) {
        shape = Shape::ApplyMatrix(shape, blobMatrix);
      }
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
  glyphs.clear();
  size_t globalIndex = 0;
  bool hasAnchors = !anchors.empty();
  size_t anchorsSize = anchors.size();
  for (auto run : *textBlob) {
    glyphs.reserve(glyphs.size() + run.glyphCount);
    for (size_t i = 0; i < run.glyphCount; i++) {
      Glyph glyph = {};
      glyph.glyphID = run.glyphs[i];
      glyph.font = run.font;
      glyph.matrix = GetGlyphMatrix(run, i);
      if (hasAnchors && globalIndex < anchorsSize) {
        glyph.anchor = anchors[globalIndex];
      }
      glyphs.push_back(glyph);
      globalIndex++;
    }
  }
  textBlob = nullptr;
  shape = nullptr;
}

enum class PositioningType {
  Horizontal,
  Point,
  RSXform,
  Matrix,
};

struct PositioningInfo {
  PositioningType type = PositioningType::Matrix;
  Matrix commonMatrix = Matrix::I();
  Matrix invertedCommon = Matrix::I();
  float commonY = 0;
};

static PositioningInfo DeterminePositioningInfo(const Glyph* glyphs, size_t count) {
  PositioningInfo info = {};
  if (count == 0) {
    return info;
  }

  const auto& firstMatrix = glyphs[0].matrix;
  bool allSameRotationScale = true;
  bool allCanUseRSXform = FloatNearlyEqual(firstMatrix.getScaleX(), firstMatrix.getScaleY()) &&
                          FloatNearlyEqual(firstMatrix.getSkewX(), -firstMatrix.getSkewY());

  for (size_t i = 1; i < count; i++) {
    const auto& m = glyphs[i].matrix;
    if (allSameRotationScale) {
      if (!FloatNearlyEqual(m.getScaleX(), firstMatrix.getScaleX()) ||
          !FloatNearlyEqual(m.getSkewX(), firstMatrix.getSkewX()) ||
          !FloatNearlyEqual(m.getSkewY(), firstMatrix.getSkewY()) ||
          !FloatNearlyEqual(m.getScaleY(), firstMatrix.getScaleY())) {
        allSameRotationScale = false;
      }
    }
    if (allCanUseRSXform) {
      if (!FloatNearlyEqual(m.getScaleX(), m.getScaleY()) ||
          !FloatNearlyEqual(m.getSkewX(), -m.getSkewY())) {
        allCanUseRSXform = false;
      }
    }
    if (!allSameRotationScale && !allCanUseRSXform) {
      break;
    }
  }

  if (!allSameRotationScale) {
    info.type = allCanUseRSXform ? PositioningType::RSXform : PositioningType::Matrix;
    return info;
  }

  info.commonMatrix = Matrix::MakeAll(firstMatrix.getScaleX(), firstMatrix.getSkewX(), 0,
                                      firstMatrix.getSkewY(), firstMatrix.getScaleY(), 0);
  if (!info.commonMatrix.invert(&info.invertedCommon)) {
    info.commonMatrix = Matrix::I();
    info.type = PositioningType::Matrix;
    return info;
  }

  float firstY =
      info.invertedCommon.mapXY(firstMatrix.getTranslateX(), firstMatrix.getTranslateY()).y;
  bool allSameY = true;
  for (size_t i = 1; i < count; i++) {
    const auto& m = glyphs[i].matrix;
    float y = info.invertedCommon.mapXY(m.getTranslateX(), m.getTranslateY()).y;
    if (!FloatNearlyEqual(y, firstY)) {
      allSameY = false;
      break;
    }
  }

  info.commonY = firstY;
  info.type = allSameY ? PositioningType::Horizontal : PositioningType::Point;
  return info;
}

static void FlushGlyphRun(TextBlobBuilder* builder, const Glyph* glyphs, size_t start, size_t end,
                          const PositioningInfo& info) {
  size_t count = end - start;
  if (count == 0) {
    return;
  }

  const auto& font = glyphs[start].font;

  switch (info.type) {
    case PositioningType::Horizontal: {
      const auto& buffer = builder->allocRunPosH(font, count, info.commonY);
      for (size_t i = 0; i < count; i++) {
        buffer.glyphs[i] = glyphs[start + i].glyphID;
        const auto& m = glyphs[start + i].matrix;
        buffer.positions[i] = info.invertedCommon.mapXY(m.getTranslateX(), m.getTranslateY()).x;
      }
      break;
    }
    case PositioningType::Point: {
      const auto& buffer = builder->allocRunPos(font, count);
      for (size_t i = 0; i < count; i++) {
        buffer.glyphs[i] = glyphs[start + i].glyphID;
        const auto& m = glyphs[start + i].matrix;
        auto pos = info.invertedCommon.mapXY(m.getTranslateX(), m.getTranslateY());
        buffer.positions[i * 2] = pos.x;
        buffer.positions[i * 2 + 1] = pos.y;
      }
      break;
    }
    case PositioningType::RSXform: {
      const auto& buffer = builder->allocRunRSXform(font, count);
      auto* xforms = reinterpret_cast<RSXform*>(buffer.positions);
      for (size_t i = 0; i < count; i++) {
        buffer.glyphs[i] = glyphs[start + i].glyphID;
        const auto& m = glyphs[start + i].matrix;
        xforms[i] =
            RSXform::Make(m.getScaleX(), m.getSkewY(), m.getTranslateX(), m.getTranslateY());
      }
      break;
    }
    case PositioningType::Matrix: {
      const auto& buffer = builder->allocRunMatrix(font, count);
      for (size_t i = 0; i < count; i++) {
        buffer.glyphs[i] = glyphs[start + i].glyphID;
        const auto& m = glyphs[start + i].matrix;
        auto* p = buffer.positions + i * 6;
        p[0] = m.getScaleX();
        p[1] = m.getSkewX();
        p[2] = m.getTranslateX();
        p[3] = m.getSkewY();
        p[4] = m.getScaleY();
        p[5] = m.getTranslateY();
      }
      break;
    }
  }
}

static Geometry::TextBlobResult BuildTextBlob(const Glyph* glyphs, size_t count) {
  Geometry::TextBlobResult result = {};
  if (count == 0) {
    return result;
  }

  auto info = DeterminePositioningInfo(glyphs, count);

  TextBlobBuilder builder;
  size_t runStart = 0;
  auto currentFont = glyphs[0].font;

  for (size_t i = 1; i < count; i++) {
    if (glyphs[i].font != currentFont) {
      FlushGlyphRun(&builder, glyphs, runStart, i, info);
      runStart = i;
      currentFont = glyphs[i].font;
    }
  }
  FlushGlyphRun(&builder, glyphs, runStart, count, info);

  result.blob = builder.build();
  result.commonMatrix = info.commonMatrix;
  return result;
}

Geometry::TextBlobResult Geometry::buildTextBlob(const std::vector<Glyph>& inputGlyphs) {
  return BuildTextBlob(inputGlyphs.data(), inputGlyphs.size());
}

void Geometry::buildGlyphRuns() {
  glyphRuns.clear();
  if (glyphs.empty()) {
    if (textBlob) {
      glyphRuns.push_back({textBlob, Matrix::I(), {}});
    }
    return;
  }

  const Glyph* data = glyphs.data();
  size_t count = glyphs.size();
  size_t runStart = 0;
  auto currentStyle = data[0].style;

  for (size_t i = 1; i < count; i++) {
    if (data[i].style != currentStyle) {
      auto result = BuildTextBlob(data + runStart, i - runStart);
      if (result.blob) {
        glyphRuns.push_back({std::move(result.blob), result.commonMatrix, currentStyle});
      }
      runStart = i;
      currentStyle = data[i].style;
    }
  }

  auto result = BuildTextBlob(data + runStart, count - runStart);
  if (result.blob) {
    glyphRuns.push_back({std::move(result.blob), result.commonMatrix, currentStyle});
  }
}

}  // namespace tgfx

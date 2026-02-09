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

#include "tgfx/layers/vectors/TextPath.h"
#include <cmath>
#include "Geometry.h"
#include "VectorContext.h"
#include "core/utils/MathExtra.h"
#include "tgfx/core/PathMeasure.h"

namespace tgfx {

std::shared_ptr<TextPath> TextPath::Make() {
  return std::shared_ptr<TextPath>(new TextPath());
}

void TextPath::setPath(Path value) {
  if (_path == value) {
    return;
  }
  _path = std::move(value);
  invalidateContent();
}

void TextPath::setBaselineOrigin(Point value) {
  if (_baselineOrigin == value) {
    return;
  }
  _baselineOrigin = value;
  invalidateContent();
}

void TextPath::setFirstMargin(float value) {
  if (_firstMargin == value) {
    return;
  }
  _firstMargin = value;
  invalidateContent();
}

void TextPath::setLastMargin(float value) {
  if (_lastMargin == value) {
    return;
  }
  _lastMargin = value;
  invalidateContent();
}

void TextPath::setPerpendicular(bool value) {
  if (_perpendicular == value) {
    return;
  }
  _perpendicular = value;
  invalidateContent();
}

void TextPath::setReversed(bool value) {
  if (_reversed == value) {
    return;
  }
  _reversed = value;
  invalidateContent();
}

void TextPath::setBaselineAngle(float value) {
  if (_baselineAngle == value) {
    return;
  }
  _baselineAngle = value;
  invalidateContent();
}

void TextPath::setForceAlignment(bool value) {
  if (_forceAlignment == value) {
    return;
  }
  _forceAlignment = value;
  invalidateContent();
}

static bool GetPosTanExtended(PathMeasure* pathMeasure, float distance, float pathLength,
                              Point* position, Point* tangent) {
  if (distance >= 0.0f && distance <= pathLength) {
    return pathMeasure->getPosTan(distance, position, tangent);
  }
  if (distance < 0.0f) {
    if (!pathMeasure->getPosTan(0.0f, position, tangent)) {
      return false;
    }
    position->x += tangent->x * distance;
    position->y += tangent->y * distance;
  } else {
    if (!pathMeasure->getPosTan(pathLength, position, tangent)) {
      return false;
    }
    auto overflow = distance - pathLength;
    position->x += tangent->x * overflow;
    position->y += tangent->y * overflow;
  }
  return true;
}

// Collect all glyphs from geometries into a flat list with geometry info
struct GlyphEntry {
  Glyph* glyph = nullptr;
  Geometry* geometry = nullptr;
  Matrix invertedMatrix = Matrix::I();
  Matrix geometryMatrix = Matrix::I();
};

static std::vector<GlyphEntry> CollectGlyphs(const std::vector<Geometry*>& glyphGeometries) {
  std::vector<GlyphEntry> result = {};
  for (auto* geometry : glyphGeometries) {
    if (geometry->glyphs.empty()) {
      continue;
    }
    Matrix invertedMatrix = Matrix::I();
    if (!geometry->matrix.invert(&invertedMatrix)) {
      invertedMatrix = Matrix::I();
    }
    for (auto& glyph : geometry->glyphs) {
      result.push_back({&glyph, geometry, invertedMatrix, geometry->matrix});
    }
  }
  return result;
}

// Returns the glyph's translation component mapped to group coordinates.
static Point GetGlyphOrigin(const Glyph& glyph, const Matrix& geometryMatrix) {
  return geometryMatrix.mapXY(glyph.matrix.getTranslateX(), glyph.matrix.getTranslateY());
}

// Returns the glyph's anchor point mapped through both glyph and geometry matrices to group
// coordinates.
static Point GetGlyphAnchor(const Glyph& glyph, const Matrix& geometryMatrix) {
  auto localPos = glyph.matrix.mapXY(glyph.anchor.x, glyph.anchor.y);
  return geometryMatrix.mapXY(localPos.x, localPos.y);
}

// Adjusts pathOffset for reversed direction and closed path wrapping.
static float AdjustPathOffset(float pathOffset, float pathLength, bool reversed, bool isClosed) {
  if (reversed) {
    pathOffset = pathLength - pathOffset;
  }
  if (isClosed && (pathOffset < 0.0f || pathOffset > pathLength)) {
    pathOffset = std::fmod(std::fmod(pathOffset, pathLength) + pathLength, pathLength);
  }
  return pathOffset;
}

// Computes the curve-following rotation angle based on tangent direction.
static float ComputeRotationAngle(const Point& tangent, bool perpendicular, bool reversed,
                                  float baselineAngle) {
  if (!perpendicular) {
    return 0.0f;
  }
  auto angle = RadiansToDegrees(std::atan2(tangent.y, tangent.x));
  if (reversed) {
    angle += 180.0f;
  }
  return angle - baselineAngle;
}

// Extracts the rotation/scale component from a glyph matrix (strips translation).
static Matrix ExtractRotationScale(const Matrix& glyphMatrix) {
  Matrix result = glyphMatrix;
  result.setTranslateX(0);
  result.setTranslateY(0);
  return result;
}

// Builds the final glyph matrix: rotationScale * curveRotation + translate, then converts back to
// geometry local coordinates via invertedMatrix.
static void BuildGlyphMatrix(Glyph& glyph, const Matrix& rotationScale, float rotationAngle,
                             const Point& glyphOrigin, const Matrix& invertedMatrix) {
  Matrix curveRotation = Matrix::I();
  curveRotation.setRotate(rotationAngle);
  glyph.matrix = rotationScale;
  glyph.matrix.postConcat(curveRotation);
  glyph.matrix.postTranslate(glyphOrigin.x, glyphOrigin.y);
  glyph.matrix.postConcat(invertedMatrix);
}

void TextPath::apply(VectorContext* context) {
  if (_path.isEmpty()) {
    return;
  }
  auto pathMeasure = PathMeasure::MakeFrom(_path);
  if (pathMeasure == nullptr) {
    return;
  }
  auto pathLength = pathMeasure->getLength();
  if (pathLength <= 0.0f) {
    return;
  }
  auto isClosed = pathMeasure->isClosed();
  auto glyphGeometries = context->getGlyphGeometries();
  auto allGlyphs = CollectGlyphs(glyphGeometries);
  if (allGlyphs.empty()) {
    return;
  }

  if (_forceAlignment) {
    // ForceAlignment mode: ignores original glyph positions and baselineOrigin. Glyphs are laid out
    // consecutively using their advance widths, with spacing adjusted to fill the available path
    // length between firstMargin and lastMargin. Each glyph's anchor is placed at the center of its
    // advance on the curve.
    auto availableLength = pathLength + _lastMargin - _firstMargin;

    float totalAdvance = 0.0f;
    for (const auto& entry : allGlyphs) {
      totalAdvance += entry.glyph->font.getAdvance(entry.glyph->glyphID);
    }

    float extraSpacingPerGap = 0.0f;
    if (allGlyphs.size() > 1 && totalAdvance > 0.0f) {
      float extraSpace = availableLength - totalAdvance;
      extraSpacingPerGap = extraSpace / static_cast<float>(allGlyphs.size() - 1);
    }

    float accumulatedAdvance = 0.0f;
    for (size_t i = 0; i < allGlyphs.size(); i++) {
      auto& entry = allGlyphs[i];
      auto& glyph = *entry.glyph;
      float advance = glyph.font.getAdvance(glyph.glyphID);

      float pathOffset = _firstMargin + advance * 0.5f + accumulatedAdvance +
                         static_cast<float>(i) * extraSpacingPerGap;
      pathOffset = AdjustPathOffset(pathOffset, pathLength, _reversed, isClosed);

      Point position = {};
      Point tangent = {};
      if (!GetPosTanExtended(pathMeasure.get(), pathOffset, pathLength, &position, &tangent)) {
        accumulatedAdvance += advance;
        continue;
      }

      auto rotationAngle = ComputeRotationAngle(tangent, _perpendicular, _reversed, _baselineAngle);
      auto rotationScale = ExtractRotationScale(glyph.matrix);

      // Anchor offset in glyph's local rotation/scale space
      auto localAnchor = rotationScale.mapXY(glyph.anchor.x, glyph.anchor.y);

      // Rotate local anchor to curve space, then place glyph so that the anchor lands on the curve
      Matrix curveRotation = Matrix::I();
      curveRotation.setRotate(rotationAngle);
      auto rotatedAnchor = curveRotation.mapXY(localAnchor.x, localAnchor.y);
      Point glyphOrigin = {position.x - rotatedAnchor.x, position.y - rotatedAnchor.y};

      BuildGlyphMatrix(glyph, rotationScale, rotationAngle, glyphOrigin, entry.invertedMatrix);

      accumulatedAdvance += advance;
    }
  } else {
    // Normal mode: projects each glyph's anchor onto the curve, then translates the entire glyph
    // so its anchor lands directly on the curve. The glyph rotates around its anchor to follow
    // the curve tangent.
    //
    // 1. tangentDistance = anchor's displacement from baselineOrigin projected along the baseline
    //    direction, determines the position along the curve.
    // 2. anchorNew is the curve point at that position (anchor always lands on the curve).
    // 3. The glyph is rotated around anchorOld, then translated so anchorOld moves to anchorNew.
    auto origin = _baselineOrigin;

    float rotationRadians = _baselineAngle * static_cast<float>(M_PI) / 180.0f;
    float cosR = std::cos(rotationRadians);
    float sinR = std::sin(rotationRadians);

    for (size_t idx = 0; idx < allGlyphs.size(); idx++) {
      auto& entry = allGlyphs[idx];
      auto& glyph = *entry.glyph;

      auto anchorOld = GetGlyphAnchor(glyph, entry.geometryMatrix);
      auto glyphOriginOld = GetGlyphOrigin(glyph, entry.geometryMatrix);

      // tangentDistance from anchor: determines where along the curve
      float dx = anchorOld.x - origin.x;
      float dy = anchorOld.y - origin.y;
      float tangentDistance = dx * cosR + dy * sinR;

      float pathOffset = _firstMargin + tangentDistance;
      pathOffset = AdjustPathOffset(pathOffset, pathLength, _reversed, isClosed);

      Point position = {};
      Point tangent = {};
      if (!GetPosTanExtended(pathMeasure.get(), pathOffset, pathLength, &position, &tangent)) {
        continue;
      }

      // anchorNew is directly on the curve
      Point anchorNew = position;

      auto rotationAngle = ComputeRotationAngle(tangent, _perpendicular, _reversed, _baselineAngle);
      auto rotationScale = ExtractRotationScale(glyph.matrix);

      // Rotate glyphOrigin around anchorOld, then translate so anchorOld moves to anchorNew
      Matrix transform = Matrix::I();
      transform.postTranslate(-anchorOld.x, -anchorOld.y);
      transform.postRotate(rotationAngle);
      transform.postTranslate(anchorNew.x, anchorNew.y);
      Point glyphOriginNew = transform.mapXY(glyphOriginOld.x, glyphOriginOld.y);

      BuildGlyphMatrix(glyph, rotationScale, rotationAngle, glyphOriginNew, entry.invertedMatrix);
    }
  }
}

}  // namespace tgfx

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

static float AdjustPathOffset(float pathOffset, float pathLength, bool reversed, bool isClosed) {
  if (reversed) {
    pathOffset = pathLength - pathOffset;
  }
  if (isClosed && (pathOffset < 0.0f || pathOffset > pathLength)) {
    pathOffset = std::fmod(std::fmod(pathOffset, pathLength) + pathLength, pathLength);
  }
  return pathOffset;
}

static void PlaceGlyphOnCurve(Glyph& glyph, const Point& curvePoint, const Point& curveTangent,
                               bool perpendicular, bool reversed, float baselineCos,
                               float baselineSin, const Matrix& invertedMatrix) {
  auto rotationScale = glyph.matrix;
  rotationScale.setTranslateX(0);
  rotationScale.setTranslateY(0);
  auto localAnchor = rotationScale.mapXY(glyph.anchor.x, glyph.anchor.y);
  if (perpendicular) {
    // Compute sin/cos of (curveAngle - baselineAngle) using the compound angle formula.
    // curveTangent is the unit direction vector (cos, sin) of the curve at this point.
    // Negating it for reversed paths is equivalent to adding 180 degrees.
    float curveC = reversed ? -curveTangent.x : curveTangent.x;
    float curveS = reversed ? -curveTangent.y : curveTangent.y;
    float finalCos = curveC * baselineCos + curveS * baselineSin;
    float finalSin = curveS * baselineCos - curveC * baselineSin;
    Matrix curveRotation = Matrix::I();
    curveRotation.setSinCos(finalSin, finalCos);
    auto rotatedAnchor = curveRotation.mapXY(localAnchor.x, localAnchor.y);
    glyph.matrix = rotationScale;
    glyph.matrix.postConcat(curveRotation);
    glyph.matrix.postTranslate(curvePoint.x - rotatedAnchor.x, curvePoint.y - rotatedAnchor.y);
  } else {
    glyph.matrix = rotationScale;
    glyph.matrix.postTranslate(curvePoint.x - localAnchor.x, curvePoint.y - localAnchor.y);
  }
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

  if (_forceAlignment) {
    // ForceAlignment mode: ignores original glyph positions and baselineOrigin. Glyphs are laid out
    // consecutively using their advance widths, with spacing adjusted to fill the available path
    // length between firstMargin and lastMargin. Each glyph's anchor is placed at the center of its
    // advance on the curve.
    size_t glyphCount = 0;
    float totalAdvance = 0.0f;
    for (auto* geometry : glyphGeometries) {
      for (auto& glyph : geometry->glyphs) {
        totalAdvance += glyph.font.getAdvance(glyph.glyphID);
        glyphCount++;
      }
    }
    if (glyphCount == 0) {
      return;
    }

    auto availableLength = pathLength + _lastMargin - _firstMargin;
    float extraSpacingPerGap = 0.0f;
    if (glyphCount > 1 && totalAdvance > 0.0f) {
      extraSpacingPerGap =
          (availableLength - totalAdvance) / static_cast<float>(glyphCount - 1);
    }

    float baselineRadians = _baselineAngle * static_cast<float>(M_PI) / 180.0f;
    float baselineCos = std::cos(baselineRadians);
    float baselineSin = std::sin(baselineRadians);

    size_t glyphIndex = 0;
    float accumulatedAdvance = 0.0f;
    for (auto* geometry : glyphGeometries) {
      if (geometry->glyphs.empty()) {
        continue;
      }
      Matrix invertedMatrix = Matrix::I();
      geometry->matrix.invert(&invertedMatrix);
      for (auto& glyph : geometry->glyphs) {
        float advance = glyph.font.getAdvance(glyph.glyphID);
        float pathOffset = _firstMargin + advance * 0.5f + accumulatedAdvance +
                           static_cast<float>(glyphIndex) * extraSpacingPerGap;
        pathOffset = AdjustPathOffset(pathOffset, pathLength, _reversed, isClosed);

        Point position = {};
        Point tangent = {};
        if (GetPosTanExtended(pathMeasure.get(), pathOffset, pathLength, &position, &tangent)) {
          PlaceGlyphOnCurve(glyph, position, tangent, _perpendicular, _reversed, baselineCos,
                            baselineSin, invertedMatrix);
        }
        accumulatedAdvance += advance;
        glyphIndex++;
      }
    }
  } else {
    // Normal mode: each glyph's anchor displacement from baselineOrigin is decomposed into
    // tangent and normal components relative to the baseline. The tangent component maps to a
    // position along the curve, and the normal component is preserved as perpendicular offset.
    float baselineRadians = _baselineAngle * static_cast<float>(M_PI) / 180.0f;
    float baselineCos = std::cos(baselineRadians);
    float baselineSin = std::sin(baselineRadians);

    for (auto* geometry : glyphGeometries) {
      if (geometry->glyphs.empty()) {
        continue;
      }
      Matrix invertedMatrix = Matrix::I();
      geometry->matrix.invert(&invertedMatrix);
      for (auto& glyph : geometry->glyphs) {
        auto localPos = glyph.matrix.mapXY(glyph.anchor.x, glyph.anchor.y);
        auto anchorOld = geometry->matrix.mapXY(localPos.x, localPos.y);

        float dx = anchorOld.x - _baselineOrigin.x;
        float dy = anchorOld.y - _baselineOrigin.y;
        float tangentDistance = dx * baselineCos + dy * baselineSin;
        float normalOffset = dy * baselineCos - dx * baselineSin;

        float pathOffset = _firstMargin + tangentDistance;
        pathOffset = AdjustPathOffset(pathOffset, pathLength, _reversed, isClosed);

        Point position = {};
        Point tangent = {};
        if (!GetPosTanExtended(pathMeasure.get(), pathOffset, pathLength, &position, &tangent)) {
          continue;
        }

        Point anchorNew = {position.x - tangent.y * normalOffset,
                           position.y + tangent.x * normalOffset};
        PlaceGlyphOnCurve(glyph, anchorNew, tangent, _perpendicular, _reversed, baselineCos,
                           baselineSin, invertedMatrix);
      }
    }
  }
}

}  // namespace tgfx

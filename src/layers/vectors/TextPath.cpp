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

// Get the position of a glyph's origin in group coordinates.
static Point GetGlyphOrigin(const Glyph& glyph, const Matrix& geometryMatrix) {
  return geometryMatrix.mapXY(glyph.matrix.getTranslateX(), glyph.matrix.getTranslateY());
}

// Get the position of a glyph's anchor point in group coordinates.
static Point GetGlyphAnchor(const Glyph& glyph, const Matrix& geometryMatrix) {
  auto localPos = glyph.matrix.mapXY(glyph.anchor.x, glyph.anchor.y);
  return geometryMatrix.mapXY(localPos.x, localPos.y);
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

  // Calculate available path length
  auto availableLength = pathLength + _lastMargin - _firstMargin;

  // Precompute rotation values
  float rotationRadians = _baselineAngle * static_cast<float>(M_PI) / 180.0f;
  float cosR = std::cos(rotationRadians);
  float sinR = std::sin(rotationRadians);

  // baselineOrigin is an offset relative to the path start point. Convert it to a local
  // coordinate by adding the path start position, so it serves as the baseline reference point.
  Point pathStart = {};
  pathMeasure->getPosTan(0.0f, &pathStart, nullptr);
  auto origin = pathStart + _baselineOrigin;

  if (_forceAlignment) {
    // ForceAlignment mode: lay out glyphs using advance, then adjust spacing
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

      // Anchor position on path: firstMargin + advance/2 + accumulated advance + extra spacing
      // The anchor is placed at the center of each character's advance
      float pathOffset = _firstMargin + advance * 0.5f + accumulatedAdvance +
                         static_cast<float>(i) * extraSpacingPerGap;

      if (_reversed) {
        pathOffset = pathLength - pathOffset;
      }

      if (isClosed && (pathOffset < 0.0f || pathOffset > pathLength)) {
        pathOffset = std::fmod(std::fmod(pathOffset, pathLength) + pathLength, pathLength);
      }

      Point position = {};
      Point tangent = {};
      if (!GetPosTanExtended(pathMeasure.get(), pathOffset, pathLength, &position, &tangent)) {
        accumulatedAdvance += advance;
        continue;
      }

      Point anchorFinalPos = position;

      Matrix rotationScale = glyph.matrix;
      rotationScale.setTranslateX(0);
      rotationScale.setTranslateY(0);
      
      // Transform anchor offset by glyph's own rotation/scale
      auto transformedAnchor = rotationScale.mapXY(glyph.anchor.x, glyph.anchor.y);

      // Build curve rotation matrix
      Matrix curveRotation = Matrix::I();
      if (_perpendicular) {
        auto angle = RadiansToDegrees(std::atan2(tangent.y, tangent.x));
        if (_reversed) {
          angle += 180.0f;
        }
        angle -= _baselineAngle;
        curveRotation.setRotate(angle);
      }

      // Rotate anchor offset to world space, then subtract to get glyph origin
      auto rotatedAnchor = curveRotation.mapXY(transformedAnchor.x, transformedAnchor.y);
      Point glyphOrigin = {anchorFinalPos.x - rotatedAnchor.x, anchorFinalPos.y - rotatedAnchor.y};

      // Final: rotationScale * curveRotation + translate(glyphOrigin)
      glyph.matrix = rotationScale;
      glyph.matrix.postConcat(curveRotation);
      glyph.matrix.postTranslate(glyphOrigin.x, glyphOrigin.y);
      glyph.matrix.postConcat(entry.invertedMatrix);

      accumulatedAdvance += advance;
    }
  } else {
    // Normal mode: project glyphs onto path.
    //
    // Algorithm:
    // 1. Calculate anchor's new position on the curve (anchorTarget)
    // 2. Calculate glyph origin relative to anchor, then rotate around anchor
    // 3. New glyph origin = anchorTarget + rotated relative offset
    //
    // Key insight: use anchor position to determine tangent distance (path position),
    // but use glyph origin to determine normal offset (perpendicular distance from curve).
    // This ensures anchor Y offset affects glyph position via relativeToAnchor,
    // not via normalOffset (which would cause double-counting and cancellation).
    for (size_t idx = 0; idx < allGlyphs.size(); idx++) {
      auto& entry = allGlyphs[idx];
      auto& glyph = *entry.glyph;

      // Get anchor and origin positions in group coordinates
      auto anchorPos = GetGlyphAnchor(glyph, entry.geometryMatrix);
      auto glyphOriginOld = GetGlyphOrigin(glyph, entry.geometryMatrix);
      
      // Use anchor for tangent distance (determines position along curve)
      float anchorDx = anchorPos.x - origin.x;
      float anchorDy = anchorPos.y - origin.y;
      float tangentDistance = anchorDx * cosR + anchorDy * sinR;
      
      // Use glyph origin for normal offset (determines perpendicular offset from curve)
      float originDx = glyphOriginOld.x - origin.x;
      float originDy = glyphOriginOld.y - origin.y;
      float normalOffset = originDy * cosR - originDx * sinR;

      float pathOffset = _firstMargin + tangentDistance;

      if (_reversed) {
        pathOffset = pathLength - pathOffset;
      }

      if (isClosed && (pathOffset < 0.0f || pathOffset > pathLength)) {
        pathOffset = std::fmod(std::fmod(pathOffset, pathLength) + pathLength, pathLength);
      }

      Point position = {};
      Point tangent = {};
      if (!GetPosTanExtended(pathMeasure.get(), pathOffset, pathLength, &position, &tangent)) {
        continue;
      }

      // Anchor's new position: curve position + normal offset perpendicular to path
      float normalX = -tangent.y * normalOffset;
      float normalY = tangent.x * normalOffset;
      Point anchorTarget = {position.x + normalX, position.y + normalY};
      
      // Calculate rotation angle for perpendicular mode
      float rotationAngle = 0.0f;
      if (_perpendicular) {
        rotationAngle = RadiansToDegrees(std::atan2(tangent.y, tangent.x));
        if (_reversed) {
          rotationAngle += 180.0f;
        }
        rotationAngle -= _baselineAngle;
      }

      // Get glyph's rotation/scale part (without translation)
      Matrix rotationScale = glyph.matrix;
      rotationScale.setTranslateX(0);
      rotationScale.setTranslateY(0);
      
      Matrix curveRotation = Matrix::I();
      curveRotation.setRotate(rotationAngle);
      
      // Glyph origin relative to anchor (in group coords, before rotation)
      auto relativeToAnchor = Point::Make(glyphOriginOld.x - anchorPos.x,
                                          glyphOriginOld.y - anchorPos.y);
      
      // New glyph origin = anchorTarget + rotated relative offset
      auto rotatedRelative = curveRotation.mapXY(relativeToAnchor.x, relativeToAnchor.y);
      Point glyphOriginNew = {anchorTarget.x + rotatedRelative.x,
                              anchorTarget.y + rotatedRelative.y};
      
      // Build final glyph matrix: rotationScale * curveRotation, then translate to glyphOriginNew
      glyph.matrix = rotationScale;
      glyph.matrix.postConcat(curveRotation);
      glyph.matrix.postTranslate(glyphOriginNew.x, glyphOriginNew.y);
      // Convert back to geometry's local coordinates
      glyph.matrix.postConcat(entry.invertedMatrix);
    }
  }
}

}  // namespace tgfx

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

void TextPath::setPath(Path value) {
  if (_path == value) {
    return;
  }
  _path = std::move(value);
  invalidateContent();
}

void TextPath::setAlign(TextPathAlign value) {
  if (_align == value) {
    return;
  }
  _align = value;
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

void TextPath::setPerpendicularToPath(bool value) {
  if (_perpendicularToPath == value) {
    return;
  }
  _perpendicularToPath = value;
  invalidateContent();
}

void TextPath::setReversed(bool value) {
  if (_reversed == value) {
    return;
  }
  _reversed = value;
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

  // Calculate total width
  float totalWidth = 0.0f;
  size_t totalGlyphCount = 0;
  for (auto* geometry : glyphGeometries) {
    if (geometry->glyphs.empty()) {
      continue;
    }
    totalGlyphCount += geometry->glyphs.size();
    for (const auto& glyph : geometry->glyphs) {
      totalWidth += glyph.font.getAdvance(glyph.glyphID);
    }
  }

  if (totalGlyphCount == 0) {
    return;
  }

  // lastMargin is negative to shrink from end
  auto availableLength = std::abs(pathLength + _lastMargin - _firstMargin);
  float letterSpacing = 0.0f;
  if (_forceAlignment && totalGlyphCount > 1) {
    auto glyphIntervals = static_cast<float>(totalGlyphCount - 1);
    if (_firstMargin <= pathLength + _lastMargin) {
      letterSpacing = (availableLength - totalWidth) / glyphIntervals;
    } else {
      // When firstMargin exceeds the path end, use negative spacing
      letterSpacing = -(totalWidth + availableLength) / glyphIntervals;
    }
  }

  // When forceAlignment is enabled, always start from firstMargin
  float startOffset = _firstMargin;
  if (!_forceAlignment) {
    switch (_align) {
      case TextPathAlign::Center:
        startOffset = (pathLength - totalWidth) * 0.5f + _firstMargin + _lastMargin;
        break;
      case TextPathAlign::End:
        startOffset = pathLength + _lastMargin - totalWidth;
        break;
      default:
        break;
    }
  }

  float currentPosition = startOffset;
  for (auto* geometry : glyphGeometries) {
    if (geometry->glyphs.empty()) {
      continue;
    }

    Matrix invertedMatrix = Matrix::I();
    if (!geometry->matrix.invert(&invertedMatrix)) {
      invertedMatrix = Matrix::I();
    }

    for (auto& glyph : geometry->glyphs) {
      auto advance = glyph.font.getAdvance(glyph.glyphID);
      auto halfAdvance = advance * 0.5f;
      auto centerPosition = currentPosition + halfAdvance;

      if (_reversed) {
        centerPosition = pathLength - centerPosition;
      }

      if (isClosed && (centerPosition < 0.0f || centerPosition > pathLength)) {
        centerPosition =
            std::fmod(std::fmod(centerPosition, pathLength) + pathLength, pathLength);
      }

      Point position = {};
      Point tangent = {};
      if (!GetPosTanExtended(pathMeasure.get(), centerPosition, pathLength, &position, &tangent)) {
        currentPosition += advance + letterSpacing;
        continue;
      }

      Matrix curveMatrix = Matrix::I();
      if (_perpendicularToPath) {
        auto angle = RadiansToDegrees(std::atan2(tangent.y, tangent.x));
        if (_reversed) {
          angle += 180.0f;
        }
        curveMatrix.setRotate(angle);
      }
      curveMatrix.postTranslate(position.x, position.y);

      glyph.matrix.setTranslate(-halfAdvance, 0.0f);
      glyph.matrix.postConcat(curveMatrix);
      glyph.matrix.postConcat(invertedMatrix);

      currentPosition += advance + letterSpacing;
    }
  }
}

}  // namespace tgfx

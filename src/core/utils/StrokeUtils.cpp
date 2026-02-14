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

#include "StrokeUtils.h"
#include <algorithm>
#include <cmath>
#include "core/utils/MathExtra.h"

namespace tgfx {

void ApplyStrokeToBounds(const Stroke& stroke, Rect* bounds, const Matrix& matrix,
                         bool applyMiterLimit) {
  if (bounds == nullptr) {
    return;
  }
  auto width = TreatStrokeAsHairline(stroke, matrix) ? 1.0f : stroke.width;
  auto expand = width * 0.5f;
  if (applyMiterLimit && stroke.join == LineJoin::Miter) {
    expand *= stroke.miterLimit;
  }
  expand = ceilf(expand);
  bounds->outset(expand, expand);
}

bool IsHairlineStroke(const Stroke& stroke) {
  return stroke.width <= 0 || FloatNearlyZero(stroke.width);
}

bool TreatStrokeAsHairline(const Stroke& stroke, const Matrix& matrix) {
  if (IsHairlineStroke(stroke)) {
    return true;
  }
  // If the stroke width after scaling is less than 1 pixel, treat it as hairline.
  // Use maxScale to ensure hairline rendering only when width < 1 in all directions.
  auto maxWidth = stroke.width * matrix.getMaxScale();
  return maxWidth < 1.f;
}

float GetHairlineAlphaFactor(const Stroke& stroke, const Matrix& matrix) {
  if (IsHairlineStroke(stroke)) {
    return 1.0f;
  }
  auto scaledStrokeWidth = stroke.width * matrix.getMaxScale();
  return std::clamp(scaledStrokeWidth, 0.f, 1.f);
}

std::vector<float> SimplifyLineDashPattern(const std::vector<float>& pattern,
                                           const Stroke& stroke) {
  // When LineCap is Square, the endpoints extend by half the line width.
  // If an unpainted dash segment is less than or equal to the line width, the painted segments will
  // connect seamlessly. Therefore, such a dash segment can be omitted for simplification.
  if (stroke.cap != LineCap::Square) {
    return pattern;
  }
  float addedPaintLength = 0.0f;
  std::vector<float> simplifiedDashes = {};
  for (uint32_t i = 0; i < pattern.size(); i += 2) {
    auto paintedLength = pattern[i];
    auto unpaintedLength = pattern[i + 1];
    if (unpaintedLength <= stroke.width) {
      addedPaintLength += paintedLength + unpaintedLength;
    } else {
      simplifiedDashes.push_back(paintedLength + addedPaintLength);
      simplifiedDashes.push_back(unpaintedLength);
      addedPaintLength = 0.0f;
    }
  }
  return simplifiedDashes;
}

bool StrokeLineToRect(const Stroke& stroke, const Point line[2], Rect* rect) {
  if (stroke.cap == LineCap::Round || IsHairlineStroke(stroke)) {
    return false;
  }
  if (line[0].x != line[1].x && line[0].y != line[1].y) {
    return false;
  }
  auto left = std::min(line[0].x, line[1].x);
  auto top = std::min(line[0].y, line[1].y);
  auto right = std::max(line[0].x, line[1].x);
  auto bottom = std::max(line[0].y, line[1].y);
  auto halfWidth = stroke.width / 2.0f;
  if (stroke.cap == LineCap::Square) {
    if (rect) {
      rect->setLTRB(left - halfWidth, top - halfWidth, right + halfWidth, bottom + halfWidth);
    }
    return true;
  }
  if (rect) {
    if (left == right) {
      rect->setLTRB(left - halfWidth, top, right + halfWidth, bottom);
    } else {
      rect->setLTRB(left, top - halfWidth, right, bottom + halfWidth);
    }
  }
  return true;
}

}  // namespace tgfx

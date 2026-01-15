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

#include "tgfx/layers/vectors/TrimPath.h"
#include "VectorContext.h"
#include "core/utils/Log.h"
#include "tgfx/core/PathEffect.h"
#include "tgfx/core/PathMeasure.h"

namespace tgfx {

void TrimPath::setStart(float value) {
  if (_start == value) {
    return;
  }
  _start = value;
  invalidateContent();
}

void TrimPath::setEnd(float value) {
  if (_end == value) {
    return;
  }
  _end = value;
  invalidateContent();
}

void TrimPath::setOffset(float value) {
  if (_offset == value) {
    return;
  }
  _offset = value;
  invalidateContent();
}

void TrimPath::setTrimType(TrimPathType value) {
  if (_trimType == value) {
    return;
  }
  _trimType = value;
  invalidateContent();
}

static void ApplyTrimIndividually(std::vector<Geometry*>& geometries, float start, float end) {
  // Determine if reversed (end < start)
  bool reversed = end < start;
  if (reversed) {
    // Transform to forward direction on reversed paths (matching libpag behavior)
    start = 1.0f - start;
    end = 1.0f - end;
  }

  // Normalize: shift to bring start into [0, 1) range
  float shift = std::floor(start);
  start -= shift;
  end -= shift;

  auto shapeCount = geometries.size();

  // Calculate total length (in reversed order if needed)
  float totalLength = 0;
  std::vector<float> lengths(shapeCount);
  for (size_t i = 0; i < shapeCount; i++) {
    auto index = reversed ? (shapeCount - 1 - i) : i;
    auto* geometry = geometries[index];
    if (geometry->shape == nullptr) {
      continue;
    }
    auto path = geometry->shape->getPath();
    auto pathMeasure = PathMeasure::MakeFrom(path);
    float length = 0;
    do {
      length += pathMeasure->getLength();
    } while (pathMeasure->nextContour());
    lengths[index] = length;
    totalLength += length;
  }

  if (totalLength <= 0) {
    return;
  }

  // Convert to absolute lengths
  float trimStart = start * totalLength;
  float trimEnd = end * totalLength;

  // Apply trim to each shape (in reversed order if needed)
  float addedLength = 0;
  for (size_t i = 0; i < shapeCount; i++) {
    auto index = reversed ? (shapeCount - 1 - i) : i;
    auto shapeLength = lengths[index];
    if (shapeLength <= 0) {
      continue;
    }

    float shapeStart = addedLength;
    float shapeEnd = addedLength + shapeLength;

    // Calculate local trim range for this shape
    float localStart = 0;
    float localEnd = 0;
    bool hasSegment = false;

    if (trimEnd <= totalLength) {
      // No wrap-around: single segment [trimStart, trimEnd]
      if (trimStart < shapeEnd && trimEnd > shapeStart) {
        localStart = std::max(0.0f, trimStart - shapeStart) / shapeLength;
        localEnd = std::min(shapeLength, trimEnd - shapeStart) / shapeLength;
        hasSegment = true;
      }
    } else {
      // Wrap-around: [trimStart, totalLength] + [0, trimEnd - totalLength]
      float seg1Start = trimStart;
      float seg1End = totalLength;
      float seg2Start = 0;
      float seg2End = trimEnd - totalLength;

      bool hasSeg1 = seg1Start < shapeEnd && seg1End > shapeStart;
      bool hasSeg2 = seg2Start < shapeEnd && seg2End > shapeStart;

      if (hasSeg1 && hasSeg2) {
        // Both segments intersect this shape: wrap-around within this shape
        localStart = std::max(0.0f, seg1Start - shapeStart) / shapeLength;
        localEnd = std::min(shapeLength, seg2End - shapeStart) / shapeLength + 1.0f;
        hasSegment = true;
      } else if (hasSeg1) {
        localStart = std::max(0.0f, seg1Start - shapeStart) / shapeLength;
        localEnd = std::min(shapeLength, seg1End - shapeStart) / shapeLength;
        hasSegment = true;
      } else if (hasSeg2) {
        localStart = std::max(0.0f, seg2Start - shapeStart) / shapeLength;
        localEnd = std::min(shapeLength, seg2End - shapeStart) / shapeLength;
        hasSegment = true;
      }
    }

    auto* geometry = geometries[index];
    if (hasSegment) {
      auto shape = geometry->shape;
      // When reversed, we need to reverse each shape's path before trimming
      if (reversed) {
        shape = Shape::ApplyReverse(shape);
      }
      auto trimEffect = PathEffect::MakeTrim(localStart, localEnd);
      geometry->shape = Shape::ApplyEffect(shape, trimEffect);
    } else {
      geometry->shape = nullptr;
    }

    addedLength += shapeLength;
  }
}

void TrimPath::apply(VectorContext* context) {
  DEBUG_ASSERT(context != nullptr);
  if (context->geometries.empty()) {
    return;
  }
  auto geometries = context->getShapeGeometries();

  auto offset = _offset / 360.0f;
  auto start = _start + offset;
  auto end = _end + offset;

  if (_trimType == TrimPathType::Simultaneously) {
    auto trimEffect = PathEffect::MakeTrim(start, end);
    for (auto* geometry : geometries) {
      geometry->shape = Shape::ApplyEffect(geometry->shape, trimEffect);
    }
  } else {
    ApplyTrimIndividually(geometries, start, end);
  }
}

}  // namespace tgfx

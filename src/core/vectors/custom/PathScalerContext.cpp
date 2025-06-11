/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "PathScalerContext.h"
#include "core/PathRasterizer.h"
#include "core/utils/ApplyStrokeToBound.h"
#include "tgfx/core/Shape.h"

namespace tgfx {
static constexpr float StdFakeBoldInterpKeys[] = {
    9.f,
    36.f,
};
static constexpr float StdFakeBoldInterpValues[] = {
    1.f / 24.f,
    1.f / 32.f,
};

inline float Interpolate(const float& a, const float& b, const float& t) {
  return a + (b - a) * t;
}

/**
 * Interpolate along the function described by (keys[length], values[length])
 * for the passed searchKey. SearchKeys outside the range keys[0]-keys[Length]
 * clamp to the min or max value. This function assumes the number of pairs
 * (length) will be small and a linear search is used.
 *
 * Repeated keys are allowed for discontinuous functions (so long as keys is
 * monotonically increasing). If key is the value of a repeated scalar in
 * keys the first one will be used.
 */
static float FloatInterpFunc(float searchKey, const float keys[], const float values[],
                             int length) {
  int right = 0;
  while (right < length && keys[right] < searchKey) {
    ++right;
  }
  // Could use sentinel values to eliminate conditionals, but since the
  // tables are taken as input, a simpler format is better.
  if (right == length) {
    return values[length - 1];
  }
  if (right == 0) {
    return values[0];
  }
  // Otherwise, interpolate between right - 1 and right.
  float leftKey = keys[right - 1];
  float rightKey = keys[right];
  float t = (searchKey - leftKey) / (rightKey - leftKey);
  return Interpolate(values[right - 1], values[right], t);
}

static Matrix GetTransform(bool fauxItalic, float textSize) {
  auto matrix = Matrix::MakeScale(textSize);
  if (fauxItalic) {
    matrix.postSkew(ITALIC_SKEW, 0.f);
  }
  return matrix;
}

PathScalerContext::PathScalerContext(std::shared_ptr<Typeface> typeface, float size)
    : ScalerContext(std::move(typeface), size) {
  fauxBoldScale = FloatInterpFunc(textSize, StdFakeBoldInterpKeys, StdFakeBoldInterpValues, 2);
}

FontMetrics PathScalerContext::getFontMetrics() const {
  return pathTypeFace()->fontMetrics();
}

Rect PathScalerContext::getBounds(GlyphID glyphID, bool fauxBold, bool fauxItalic) const {
  auto record = pathTypeFace()->getGlyphRecord(glyphID);
  if (record == nullptr) {
    return {};
  }
  auto bounds = record->path.getBounds();
  if (bounds.isEmpty()) {
    return {};
  }
  auto matrix = GetTransform(fauxItalic, textSize);
  bounds = matrix.mapRect(bounds);
  if (fauxBold) {
    auto fauxBoldSize = textSize * fauxBoldScale;
    bounds.outset(fauxBoldSize, fauxBoldSize);
  }
  bounds.roundOut();
  return bounds;
}

float PathScalerContext::getAdvance(GlyphID glyphID, bool) const {
  auto record = pathTypeFace()->getGlyphRecord(glyphID);
  if (record == nullptr) {
    return 0.f;
  }
  return record->advance * textSize;
}

Point PathScalerContext::getVerticalOffset(GlyphID glyphID) const {
  auto record = pathTypeFace()->getGlyphRecord(glyphID);
  if (record == nullptr) {
    return {};
  }
  return {-record->advance * 0.5f * textSize, pathTypeFace()->fontMetrics().capHeight * textSize};
}

bool PathScalerContext::generatePath(GlyphID glyphID, bool fauxBold, bool fauxItalic,
                                     Path* path) const {
  if (path == nullptr) {
    return false;
  }
  auto record = pathTypeFace()->getGlyphRecord(glyphID);
  if (record == nullptr || record->path.isEmpty()) {
    return false;
  }

  *path = record->path;
  auto transform = GetTransform(fauxItalic, textSize);
  path->transform(transform);

  if (fauxBold) {
    auto strokePath = *path;
    Stroke stroke(textSize * fauxBoldScale);
    stroke.applyToPath(&strokePath);
    path->addPath(strokePath, PathOp::Union);
  }
  return true;
}

Rect PathScalerContext::getImageTransform(GlyphID glyphID, bool fauxBold, const Stroke* stroke,
                                          Matrix* matrix) const {
  auto record = pathTypeFace()->getGlyphRecord(glyphID);
  if (record == nullptr) {
    return {};
  }
  auto bounds = getBounds(glyphID, fauxBold, false);
  if (stroke != nullptr) {
    ApplyStrokeToBounds(*stroke, &bounds, true);
  }
  if (matrix) {
    matrix->setTranslate(bounds.x(), bounds.y());
  }
  return bounds;
}

bool PathScalerContext::readPixels(GlyphID glyphID, bool fauxBold, const Stroke* stroke,
                                   const ImageInfo& dstInfo, void* dstPixels) const {
  if (dstInfo.isEmpty() || dstPixels == nullptr || dstInfo.colorType() != ColorType::ALPHA_8) {
    return false;
  }
  auto record = pathTypeFace()->getGlyphRecord(glyphID);
  if (record == nullptr || record->path.isEmpty()) {
    return false;
  }
  auto bounds = PathScalerContext::getImageTransform(glyphID, fauxBold, stroke, nullptr);
  bounds.roundOut();
  auto matrix = Matrix::MakeTrans(-bounds.x(), -bounds.y());
  auto width = static_cast<int>(bounds.width());
  auto height = static_cast<int>(bounds.height());
  if (width <= 0 || height <= 0) {
    return false;
  }
  auto shape = Shape::MakeFrom(record->path);
  shape = Shape::ApplyStroke(std::move(shape), stroke);
  shape = Shape::ApplyMatrix(std::move(shape), matrix);
  auto rasterizer = PathRasterizer::Make(width, height, std::move(shape), true, true);
  if (rasterizer == nullptr) {
    return false;
  }
  rasterizer->readPixels(dstInfo, dstPixels);
  return true;
}

PathTypeface* PathScalerContext::pathTypeFace() const {
  return static_cast<PathTypeface*>(typeface.get());
}
}  // namespace tgfx

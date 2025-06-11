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

#include "ImageScalerContext.h"
#include "core/utils/MathExtra.h"

namespace tgfx {
ImageScalerContext::ImageScalerContext(std::shared_ptr<Typeface> typeface, float size)
    : ScalerContext(std::move(typeface), size) {
  auto textScale = size;
  if (FloatNearlyZero(textScale) || !FloatsAreFinite(&textScale, 1)) {
    textScale = 1.0f;
    extraScale.set(0.0f, 0.0f);
  }
  auto fontMetrics = static_cast<ImageTypeface*>(this->typeface.get())->fontMetrics();
  auto xppem = fabs(fontMetrics.bottom - fontMetrics.top);
  auto yppem = fabs(fontMetrics.xMax - fontMetrics.xMin);
  constexpr float kDefaultPPEM = 109.f;  // Default PPI for many typefaces
  xppem = FloatNearlyZero(xppem) ? kDefaultPPEM : xppem;
  yppem = FloatNearlyZero(yppem) ? kDefaultPPEM : yppem;

  extraScale.x *= textScale / xppem;
  extraScale.y *= textScale / yppem;
}

FontMetrics ImageScalerContext::getFontMetrics() const {
  return imageTypeface()->fontMetrics();
}

Rect ImageScalerContext::getBounds(GlyphID glyphID, bool, bool fauxItalic) const {
  auto record = imageTypeface()->getGlyphRecord(glyphID);
  if (record == nullptr) {
    return {};
  }
  auto bounds =
      Rect::MakeXYWH(record->offset.x, record->offset.y, static_cast<float>(record->image->width()),
                     static_cast<float>(record->image->height()));
  auto matrix = Matrix::MakeScale(extraScale.x, extraScale.y);
  if (fauxItalic) {
    matrix.postSkew(ITALIC_SKEW, 0.f);
  }
  bounds = matrix.mapRect(bounds);
  return bounds;
}

float ImageScalerContext::getAdvance(GlyphID glyphID, bool) const {
  auto record = imageTypeface()->getGlyphRecord(glyphID);
  if (record == nullptr) {
    return 0.0f;
  }
  return record->advance * extraScale.x;
}

Point ImageScalerContext::getVerticalOffset(GlyphID glyphID) const {
  auto record = imageTypeface()->getGlyphRecord(glyphID);
  if (record == nullptr) {
    return {};
  }
  return {-record->advance * 0.5f * extraScale.y, imageTypeface()->fontMetrics().capHeight};
}
bool ImageScalerContext::generatePath(GlyphID, bool, bool, Path*) const {
  return false;
}

Rect ImageScalerContext::getImageTransform(GlyphID glyphID, bool, const Stroke*,
                                           Matrix* matrix) const {
  auto record = imageTypeface()->getGlyphRecord(glyphID);
  if (record == nullptr || record->image == nullptr) {
    return {};
  }

  if (matrix) {
    matrix->setTranslate(record->offset.x, record->offset.y);
    matrix->postScale(extraScale.x, extraScale.y);
  }
  return Rect::MakeXYWH(record->offset.x, record->offset.y,
                        static_cast<float>(record->image->width()),
                        static_cast<float>(record->image->height()));
}

bool ImageScalerContext::readPixels(GlyphID glyphID, bool, const Stroke*, const ImageInfo& dstInfo,
                                    void* dstPixels) const {
  if (dstInfo.isEmpty() || dstPixels == nullptr) {
    return false;
  }
  auto record = imageTypeface()->getGlyphRecord(glyphID);
  if (record == nullptr || record->image == nullptr) {
    return false;
  }
  return record->image->readPixels(dstInfo, dstPixels);
}

ImageTypeface* ImageScalerContext::imageTypeface() const {
  return static_cast<ImageTypeface*>(typeface.get());
}
}  // namespace tgfx

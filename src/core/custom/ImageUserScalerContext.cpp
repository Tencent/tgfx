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

#include "ImageUserScalerContext.h"
#include "core/utils/MathExtra.h"

namespace tgfx {
ImageUserScalerContext::ImageUserScalerContext(std::shared_ptr<Typeface> typeface, float size)
    : UserScalerContext(std::move(typeface), size) {
  auto textScale = size;
  if (FloatNearlyZero(textScale) || !FloatsAreFinite(&textScale, 1)) {
    textScale = 1.0f;
    extraScale.set(1.0f, 1.0f);
  }
  auto imageUserTypeface = static_cast<ImageUserTypeface*>(this->typeface.get());
  auto fontMetrics = imageUserTypeface->fontMetrics();
  auto xPpem = fabs(fontMetrics.bottom - fontMetrics.top);
  auto yPpem = fabs(fontMetrics.xMax - fontMetrics.xMin);
  if (FloatNearlyZero(xPpem) || FloatNearlyZero(yPpem)) {
    auto record = imageUserTypeface->getGlyphRecord(1);
    if (record != nullptr && record->image != nullptr) {
      xPpem = static_cast<float>(record->image->width());
      yPpem = static_cast<float>(record->image->height());
    } else {
      constexpr float kDefaultPpem = 109.f;
      xPpem = kDefaultPpem;
      yPpem = kDefaultPpem;
    }
  }
  extraScale.x *= textScale / xPpem;
  extraScale.y *= textScale / yPpem;
}

Rect ImageUserScalerContext::getBounds(GlyphID glyphID, bool, bool fauxItalic) const {
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

bool ImageUserScalerContext::generatePath(GlyphID, bool, bool, Path*) const {
  return false;
}

Rect ImageUserScalerContext::getImageTransform(GlyphID glyphID, bool, const Stroke*,
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

bool ImageUserScalerContext::readPixels(GlyphID glyphID, bool, const Stroke*,
                                        const ImageInfo& dstInfo, void* dstPixels) const {
  if (dstInfo.isEmpty() || dstPixels == nullptr) {
    return false;
  }
  auto record = imageTypeface()->getGlyphRecord(glyphID);
  if (record == nullptr || record->image == nullptr) {
    return false;
  }
  return record->image->readPixels(dstInfo, dstPixels);
}

ImageUserTypeface* ImageUserScalerContext::imageTypeface() const {
  return static_cast<ImageUserTypeface*>(typeface.get());
}
}  // namespace tgfx
